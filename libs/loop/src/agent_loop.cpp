#include <merak/agent_loop.hpp>
#include <spdlog/spdlog.h>
#include <sstream>

namespace merak {

AgentLoop::AgentLoop(
    Config config,
    std::shared_ptr<LlmProvider> llm,
    std::shared_ptr<ToolRegistry> tools,
    std::shared_ptr<MemoryStore> memory,
    std::shared_ptr<ContextAssembler> context,
    std::shared_ptr<Compactor> compactor)
    : config_(std::move(config))
    , llm_(std::move(llm))
    , tools_(std::move(tools))
    , memory_(std::move(memory))
    , context_(std::move(context))
    , compactor_(std::move(compactor))
    , counter_(std::make_shared<TokenCounter>())
{
}

void AgentLoop::transition_to(TurnState next, RunControl& control) {
    auto prev = state_;
    state_ = next;
    control.emit_state(prev, next);
    spdlog::debug("Loop: {} → {}", state_name(prev), state_name(next));
}

std::future<AgentResponse> AgentLoop::run(
    const std::string& user_message,
    RunControl& control,
    std::vector<Message> initial_history,
    bool append_user_message) {
    return std::async(std::launch::async,
        [this, user_message, &control, initial_history = std::move(initial_history),
         append_user_message]() mutable
        -> AgentResponse {
        AgentResponse response;
        session_history_ = std::move(initial_history);

        if (append_user_message) {
            Message user_msg;
            user_msg.role = "user";
            user_msg.content = user_message;
            session_history_.push_back(user_msg);
            memory_->append_message(user_msg);
            control.append_message(user_msg);
        }

        tool_failure_streak_.clear();

        transition_to(TurnState::ContextReady, control);
        int turn_count = 0;

        while (turn_count < config_.max_turns) {
            if (config_.enable_compaction) {
                maybe_compact(control);
            }
            if (control.cancelled()) throw AgentError(ErrorType::INTERNAL_ERROR, "Run cancelled");

            auto context_messages = build_context(user_message);

            auto split = CacheAwareContext::split(context_messages);
            spdlog::debug("Loop: turn {} — {}", turn_count,
                CacheAwareContext::info(split));

            transition_to(TurnState::Thinking, control);
            turn_count++;

            ChatRequest req;
            req.model = config_.default_model;
            req.max_output_tokens = config_.max_output_tokens;
            req.messages = context_messages;
            req.enable_cache = config_.enable_cache;

            auto tool_specs = tools_->all_tools();
            req.tools = tool_specs;

            std::vector<ToolCall> accumulated_tool_calls;

            auto llm_future = llm_->chat(req,
                [&](StreamChunk chunk) {
                    if (chunk.is_final) return;
                    if (!chunk.is_tool_call) {
                        control.emit_text_delta(chunk.text);
                        response.text += chunk.text;
                    }
                }, control.cancellation_token());

            auto llm_response = llm_future.get();
            response.total_input_tokens += llm_response.total_input_tokens;
            response.total_output_tokens += llm_response.total_output_tokens;
            response.has_usage = response.has_usage || llm_response.has_usage;
            response.usage_missing = response.usage_missing || !llm_response.has_usage;
            control.emit_usage(llm_response.total_input_tokens,
                llm_response.total_output_tokens, llm_response.has_usage);
            if (control.cancelled()) throw AgentError(ErrorType::INTERNAL_ERROR, "Run cancelled");

            accumulated_tool_calls = llm_response.tool_calls;

            if (accumulated_tool_calls.empty()) {
                transition_to(TurnState::Responding, control);

                Message assistant_msg;
                assistant_msg.role = "assistant";
                assistant_msg.content = llm_response.text;
                assistant_msg.tool_calls = accumulated_tool_calls;
                assistant_msg.provider_content_blocks_json =
                    llm_response.provider_content_blocks_json;
                session_history_.push_back(assistant_msg);
                memory_->append_message(assistant_msg);
                control.append_message(assistant_msg);

                transition_to(TurnState::Complete, control);
                return response;
            }

            transition_to(TurnState::Acting, control);

            Message assistant_msg;
            assistant_msg.role = "assistant";
            assistant_msg.content = llm_response.text;
            assistant_msg.tool_calls = accumulated_tool_calls;
            assistant_msg.provider_content_blocks_json =
                llm_response.provider_content_blocks_json;
            session_history_.push_back(assistant_msg);
            memory_->append_message(assistant_msg);
            control.append_message(assistant_msg);

            auto tool_results = handle_tool_calls(accumulated_tool_calls, control);

            for (auto& tr : tool_results) {
                response.tool_results.push_back(tr);

                Message tool_msg;
                tool_msg.role = "tool";
                tool_msg.content = tr.output;
                tool_msg.tool_call_id = tr.call_id;
                session_history_.push_back(tool_msg);
                memory_->append_message(tool_msg);
                control.append_message(tool_msg);
            }

            transition_to(TurnState::Observing, control);
            transition_to(TurnState::ContextReady, control);
        }

        spdlog::warn("Loop: max turns ({}) reached, forcing text response",
            config_.max_turns);

        transition_to(TurnState::Thinking, control);
        ChatRequest final_req;
        final_req.model = config_.default_model;
        final_req.max_output_tokens = config_.max_output_tokens;
        final_req.messages = build_context(
            "Please provide your final answer as text. Do not use any tools.");
        final_req.tools = {};
        final_req.enable_cache = false;

        auto final_future = llm_->chat(final_req,
            [&](StreamChunk chunk) {
                if (!chunk.is_final) control.emit_text_delta(chunk.text);
                response.text += chunk.text;
            }, control.cancellation_token());
        auto final_resp = final_future.get();
        response.text = final_resp.text;
        response.total_input_tokens += final_resp.total_input_tokens;
        response.total_output_tokens += final_resp.total_output_tokens;
        response.has_usage = response.has_usage || final_resp.has_usage;
        response.usage_missing = response.usage_missing || !final_resp.has_usage;
        control.emit_usage(final_resp.total_input_tokens,
            final_resp.total_output_tokens, final_resp.has_usage);

        transition_to(TurnState::Complete, control);
        return response;
    });
}

std::vector<Message> AgentLoop::build_context(const std::string& user_message) {
    auto mem_future = memory_->search(user_message, 5);
    std::vector<MemorySnippet> mem_snippets;
    if (mem_future.valid()) {
        auto mem_result = mem_future.get();
        if (mem_result.has_value()) {
            mem_snippets = mem_result.value();
        }
    }

    return context_->assemble(
        config_.system_prompt,
        tools_->all_tools_json(),
        session_history_,
        mem_snippets
    );
}

std::vector<ToolResult> AgentLoop::handle_tool_calls(
    const std::vector<ToolCall>& calls,
    RunControl& control
) {
    std::vector<ToolResult> results;

    for (auto& call : calls) {
        if (control.cancelled()) break;
        control.emit_tool_started(call);

        // 熔断器检查：连续 3 次失败则跳过
        auto it = tool_failure_streak_.find(call.name);
        if (it != tool_failure_streak_.end() && it->second >= kCircuitBreakerThreshold) {
            ToolResult blocked;
            blocked.call_id = call.id;
            blocked.is_error = true;
            blocked.output = "Tool '" + call.name +
                "' blocked (3 consecutive failures). Try a different approach.";
            results.push_back(blocked);
            control.emit_tool_completed(call, blocked);

            Message sys_msg;
            sys_msg.role = "system";
            sys_msg.content = blocked.output;
            session_history_.push_back(sys_msg);
            control.append_message(sys_msg);

            spdlog::warn("Circuit breaker: blocked '{}' after {} consecutive failures",
                call.name, it->second);
            continue;
        }

        if (tools_->requires_approval(call.name)) {
            bool allowed = control.await_approval(call);
            if (!allowed) {
                ToolResult denied;
                denied.call_id = call.id;
                denied.is_error = true;
                denied.output = "User denied permission for tool: " + call.name;
                results.push_back(denied);
                control.emit_tool_completed(call, denied);
                continue;
            }
        }

        auto result_future = tools_->execute(
            call, ToolExecutionContext{control.cancellation_token()});
        auto result = result_future.get();

        // 更新失败计数
        if (result.is_error) {
            tool_failure_streak_[call.name]++;
        } else {
            tool_failure_streak_[call.name] = 0;
        }

        results.push_back(result);

        control.emit_tool_completed(call, result);
    }

    return results;
}

void AgentLoop::maybe_compact(RunControl& control) {
    auto compaction_info = context_->analyze_compaction(session_history_);

    if (!compaction_info.needed) return;

    spdlog::info("Loop: triggering compaction, saving {} tokens",
        compaction_info.tokens_to_save);

    auto comp_future = compactor_->compact_history(
        session_history_,
        config_.max_turns * 2
    );

    auto comp_result = comp_future.get();

    if (!comp_result.summary.empty()) {
        Message summary_msg;
        summary_msg.role = "system";
        summary_msg.content = "[历史摘要] " + comp_result.summary;

        session_history_.insert(session_history_.begin(), summary_msg);
        control.append_message(summary_msg);
        control.record_compaction(static_cast<int>(comp_result.replaced.size()));

        if (!comp_result.replaced.empty()) {
            int erase_start = 1;
            int erase_count = (int)comp_result.replaced.size();
            if (erase_start + erase_count <= (int)session_history_.size()) {
                session_history_.erase(
                    session_history_.begin() + erase_start,
                    session_history_.begin() + erase_start + erase_count
                );
            }
        }

        std::ostringstream oss;
        for (auto& m : comp_result.replaced) {
            oss << "[" << m.role << "] " << m.content.substr(0, 200) << "\n";
        }
        memory_->store(oss.str(), "episodic");
    }
}

} // namespace merak
