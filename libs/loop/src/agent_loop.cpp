#include <merak/agent_loop.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <thread>

namespace merak {

AgentLoop::AgentLoop(
    Config config,
    std::shared_ptr<LlmProvider> llm,
    std::shared_ptr<ToolRegistry> tools,
    std::shared_ptr<MemoryStore> memory,
    std::shared_ptr<Compactor> compactor)
    : config_(std::move(config))
    , llm_(std::move(llm))
    , tools_(std::move(tools))
    , memory_(std::move(memory))
    , compactor_(std::move(compactor))
    , pipeline_(std::make_unique<ContextPipeline>())
{
    pipeline_->set_compactor(compactor_);
}

void AgentLoop::restore_history(std::vector<Message> history) {
    session_history_ = std::move(history);
}

void AgentLoop::set_system_prompt(const std::string& prompt) {
    if (!session_history_.empty() && session_history_[0].role == "system") {
        session_history_[0].content = prompt;
    } else {
        session_history_.insert(session_history_.begin(), {"system", prompt, {}, {}, ""});
    }
}

void AgentLoop::transition_to(TurnState next, RunControl& control) {
    auto prev = state_;
    state_ = next;
    control.emit_state(prev, next);
    spdlog::debug("Loop: {} → {}", state_name(prev), state_name(next));
}

std::future<AgentResponse> AgentLoop::run(
    const std::string& user_message,
    RunControl& control) {
    return std::async(std::launch::async,
        [this, user_message, &control]() -> AgentResponse {

            Message user_msg;
            user_msg.role = "user";
            user_msg.content = user_message;
            session_history_.push_back(user_msg);
            memory_->append_message(user_msg);
            control.append_message(user_msg);

            tool_failure_streak_.clear();
            turn_guard_.reset();
            stall_detector_.reset();

            return run_loop(control);
        });
}

std::future<AgentResponse> AgentLoop::resume(RunControl& control) {
    return std::async(std::launch::async,
        [this, &control]() -> AgentResponse {
            tool_failure_streak_.clear();
            turn_guard_.reset();
            stall_detector_.reset();
            return run_loop(control);
        });
}

AgentResponse AgentLoop::run_loop(RunControl& control) {
    AgentResponse response;

    transition_to(TurnState::ContextReady, control);
    int turn_count = 0;
    current_turn_ = 0;

    while (turn_count < config_.max_turns) {
        if (config_.enable_compaction) {
            maybe_compact(control);
        }
        if (auto token = control.cancellation_token(); token && token->should_stop()) throw AgentError(ErrorType::INTERNAL_ERROR, "Run cancelled");

        auto context_messages = build_context();

        if (config_.enable_cache) {
            auto split = CacheAwareContext::split(context_messages);
            spdlog::debug("Loop: turn {} — {}", turn_count,
                CacheAwareContext::info(split));
        }

        transition_to(TurnState::Thinking, control);
        turn_count++;
        current_turn_ = turn_count;

        ChatRequest req;
        req.model = config_.default_model;
        req.max_output_tokens = config_.max_output_tokens;
        req.messages = context_messages;
        req.enable_cache = config_.enable_cache;

        auto tool_specs = tools_->pinned_schemas();
        req.tools = tool_specs;

        std::vector<ToolCall> accumulated_tool_calls;

        auto llm_future = llm_->chat(req,
            [&](StreamChunk chunk) {
                auto token = control.cancellation_token();
                if (token && token->should_stop()) return;
                if (chunk.is_final) return;
                if (!chunk.is_tool_call) {
                    control.emit_text_delta(chunk.text);
                    response.text += chunk.text;
                }
            }, control.cancellation_token());

        AgentResponse llm_response;
        try {
            llm_response = llm_future.get();
        } catch (const std::exception& e) {
            auto error_class = turn_ingestor_.classify_error(0, e.what());
            switch (error_class) {
            case LlmErrorClass::Auth:
                throw AgentError(ErrorType::LLM_ERROR, "LLM authentication failed. Check your API key.");
            case LlmErrorClass::RateLimit:
                spdlog::warn("Loop: rate limited, retrying after backoff");
                std::this_thread::sleep_for(std::chrono::seconds(2));
                llm_future = llm_->chat(req,
                    [&](StreamChunk chunk) {
                        auto token = control.cancellation_token();
                        if (token && token->should_stop()) return;
                        if (chunk.is_final) return;
                        if (!chunk.is_tool_call) {
                            control.emit_text_delta(chunk.text);
                            response.text += chunk.text;
                        }
                    }, control.cancellation_token());
                llm_response = llm_future.get();
                break;
            case LlmErrorClass::ContextWindow:
                spdlog::warn("Loop: context window error, triggering compaction and retry");
                maybe_compact(control);
                llm_future = llm_->chat(req,
                    [&](StreamChunk chunk) {
                        auto token = control.cancellation_token();
                        if (token && token->should_stop()) return;
                        if (chunk.is_final) return;
                        if (!chunk.is_tool_call) {
                            control.emit_text_delta(chunk.text);
                            response.text += chunk.text;
                        }
                    }, control.cancellation_token());
                llm_response = llm_future.get();
                break;
            case LlmErrorClass::Unknown:
            default:
                throw;
            }
        }
        response.total_input_tokens += llm_response.total_input_tokens;
        response.total_output_tokens += llm_response.total_output_tokens;
        response.has_usage = response.has_usage || llm_response.has_usage;
        response.usage_missing = response.usage_missing || !llm_response.has_usage;
        control.emit_usage(llm_response.total_input_tokens,
            llm_response.total_output_tokens, llm_response.has_usage);
        if (auto token = control.cancellation_token(); token && token->should_stop()) throw AgentError(ErrorType::INTERNAL_ERROR, "Run cancelled");

        accumulated_tool_calls = llm_response.tool_calls;

        // Ingest turn for observability
        auto ingested = turn_ingestor_.ingest(
            accumulated_tool_calls.data(), accumulated_tool_calls.size(),
            {llm_response.total_input_tokens, llm_response.total_output_tokens, 0, 0},
            llm_response.text,
            std::chrono::milliseconds{0},
            turn_count
        );

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

        // Stall detection
        auto stall = stall_detector_.check(accumulated_tool_calls);
        if (stall.level == StallLevel::ForceStop) {
            spdlog::warn("Loop: force_stop triggered after 5 consecutive identical rounds");
            // Force text-only final call
            ChatRequest final_req;
            final_req.model = config_.default_model;
            final_req.max_output_tokens = config_.max_output_tokens;
            final_req.messages = build_context();
            final_req.tools = {};
            final_req.enable_cache = false;

            auto final_future = llm_->chat(final_req,
                [&](StreamChunk chunk) {
                    auto token = control.cancellation_token();
                    if (token && token->should_stop()) return;
                    if (!chunk.is_final) control.emit_text_delta(chunk.text);
                    response.text += chunk.text;
                }, control.cancellation_token());
            auto final_resp = final_future.get();
            response.text = final_resp.text;
            response.total_input_tokens += final_resp.total_input_tokens;
            response.total_output_tokens += final_resp.total_output_tokens;
            response.has_usage = response.has_usage || final_resp.has_usage;
            response.usage_missing = response.usage_missing || !final_resp.has_usage;

            transition_to(TurnState::Complete, control);
            return response;
        }

        // Execute tools
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

        if (auto token = control.cancellation_token(); token && token->should_stop()) {
            throw AgentError(ErrorType::INTERNAL_ERROR, "Run cancelled");
        }

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

        if (control.save_checkpoint) {
            nlohmann::json ts;
            ts["state"] = state_name(state_);
            ts["turn_count"] = turn_count;
            control.save_checkpoint(
                current_turn_,
                ts.dump(),
                llm_response.total_input_tokens,
                llm_response.total_output_tokens,
                "[]",
                "",
                "");
        }

        // Set total tool output chars after knowing actual results
        {
          int total_chars = 0;
          for (auto& tr : tool_results) {
            total_chars += static_cast<int>(tr.output.size());
          }
          TurnIngestor::set_tool_output_chars(ingested, total_chars);
          spdlog::debug("Loop: turn {} — {} tool calls, {} output chars, {} input/{} output tokens",
                        turn_count, ingested.tool_count, total_chars,
                        ingested.tokens.input, ingested.tokens.output);
        }

        // TurnGuard evaluation
        TurnGuard::RoundInput guard_in;
        guard_in.turn_index = turn_count;
        guard_in.tool_count = static_cast<int>(accumulated_tool_calls.size());
        guard_in.stall = stall;
        guard_in.consecutive_read_only_rounds = consecutive_read_only_rounds_;
        guard_in.consecutive_world_query_rounds = consecutive_world_query_rounds_;
        guard_in.consecutive_content_avoidance = consecutive_content_avoidance_;

        bool had_write = false;
        bool had_world_query_only = true;
        for (auto& tc : accumulated_tool_calls) {
            if (tc.name == "write_file" || tc.name == "str_replace" ||
                tc.name == "create_character" || tc.name == "create_scene" ||
                tc.name == "create_chapter" || tc.name == "add_world_knowledge" ||
                tc.name == "create_location" || tc.name == "plant_foreshadowing" ||
                tc.name == "expose_secret") {
                had_write = true;
                had_world_query_only = false;
            }
            if (tc.name != "query_map" && tc.name != "query_world" &&
                tc.name != "query_history" && tc.name != "query_magic" &&
                tc.name != "query_faction" && tc.name != "search_agent" &&
                tc.name != "look_around" && tc.name != "read_character_card" &&
                tc.name != "read_secret" && tc.name != "read_foreshadowing" &&
                tc.name != "search_my_diary" && tc.name != "read_file") {
                had_world_query_only = false;
            }
        }
        guard_in.had_write_operation = had_write;

        if (had_write) {
            consecutive_read_only_rounds_ = 0;
        } else {
            consecutive_read_only_rounds_++;
        }

        if (had_world_query_only) {
            consecutive_world_query_rounds_++;
        } else {
            consecutive_world_query_rounds_ = 0;
        }

        auto verdict = turn_guard_.evaluate(guard_in);

        if (verdict.turn_penalty) {
            config_.max_turns = std::max(1, config_.max_turns + *verdict.turn_penalty);
        }
        if (verdict.severity == Severity::Critical && turn_guard_.warning_count() >= 4) {
            spdlog::warn("Loop: TurnGuard critical after 4+ warnings, stopping");
            break;
        }
        if (verdict.nudge) {
            Message nudge_msg;
            nudge_msg.role = "system";
            nudge_msg.content = "[校正] " + *verdict.nudge;
            session_history_.push_back(nudge_msg);
            control.append_message(nudge_msg);
        }

        transition_to(TurnState::ContextReady, control);
    }

    spdlog::warn("Loop: max turns ({}) reached, forcing text response",
        config_.max_turns);

    transition_to(TurnState::Thinking, control);
    ChatRequest final_req;
    final_req.model = config_.default_model;
    final_req.max_output_tokens = config_.max_output_tokens;
    final_req.messages = build_context();
    final_req.tools = {};
    final_req.enable_cache = false;

    auto final_future = llm_->chat(final_req,
        [&](StreamChunk chunk) {
            auto token = control.cancellation_token();
            if (token && token->should_stop()) return;
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
}

void AgentLoop::set_working_memory_provider(std::function<std::string()> provider) {
    working_memory_provider_ = std::move(provider);
}

std::vector<Message> AgentLoop::build_context() {
    BindSources sources;
    sources.identity_text = [this]() {
        return config_.system_prompt.substr(0, std::min<size_t>(500, config_.system_prompt.size()));
    };
    sources.constraints_text = [this]() {
        if (config_.system_prompt.size() > 500) {
            return config_.system_prompt.substr(500);
        }
        return std::string("");
    };
    sources.world_context_text = []() { return ""; };
    sources.skills_text = []() { return ""; };
    sources.tool_specs = tools_->pinned_schemas();
    sources.working_memory_text = [this]() -> std::string {
        if (working_memory_provider_) return working_memory_provider_();
        return "";
    };
    sources.memory_store = memory_;

    for (int i = (int)session_history_.size() - 1; i >= 0; i--) {
        if (session_history_[i].role == "user") {
            sources.search_query = session_history_[i].content;
            break;
        }
    }
    sources.conversation_messages = memory_->recent_history(config_.max_turns);

    auto payload = pipeline_->planned_assemble(
        config_.system_prompt, config_.default_model,
        config_.model_max_tokens, session_history_, sources);

    return payload.messages;
}

std::vector<ToolResult> AgentLoop::handle_tool_calls(
    const std::vector<ToolCall>& calls,
    RunControl& control
) {
    std::vector<ToolResult> results;

    for (auto& call : calls) {
        // Plan mode: deny mutating tools that require approval
        if (plan_mode_) {
            auto* tool = tools_->get_tool(call.name);
            if (tool && tool->permission() == PermissionLevel::ask) {
                auto spec = tool->spec();
                if (spec.category == Category::Mutating) {
                    ToolResult denied;
                    denied.call_id = call.id;
                    denied.is_error = true;
                    denied.output = "Plan mode active — write operations restricted";
                    results.push_back(denied);
                    control.emit_tool_completed(call, denied);
                    continue;
                }
            }
        }

        auto token = control.cancellation_token();
        if (token && token->should_stop()) {
            control.record_interruption(InterruptionRecord{
                .turns_completed = current_turn_,
                .tools_completed = static_cast<int>(results.size()),
                .tools_remaining = static_cast<int>(calls.size()) - static_cast<int>(results.size()),
                .interrupted_tool_name = call.name,
                .interrupted_tool_call_id = call.id,
            });
            break;
        }
        control.emit_tool_started(call);

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
                auto tk = control.cancellation_token();
                if (tk && tk->should_stop()) {
                    control.record_interruption(InterruptionRecord{
                        .turns_completed = current_turn_,
                        .tools_completed = static_cast<int>(results.size()),
                        .tools_remaining = static_cast<int>(calls.size()) - static_cast<int>(results.size()),
                        .interrupted_tool_name = call.name,
                        .interrupted_tool_call_id = call.id,
                    });
                    break;
                }
                continue;
            }
        }

        auto result_future = tools_->execute(
            call, ToolExecutionContext{control.cancellation_token()});
        auto result = result_future.get();

        if (call.name == "ask_user") {
            try {
                auto result_json = nlohmann::json::parse(result.output);
                if (result_json.value("status", "") == "pending") {
                    result = control.await_ask_user(call, result);
                }
            } catch (...) {}
        }

        if (tools_->requires_confirmation(call.name)) {
            try {
                auto result_json = nlohmann::json::parse(result.output);
                if (result_json.contains("creation_id") && result_json.value("status", "") == "pending_creation") {
                    result = control.await_creation(call, result);
                }
            } catch (...) {
            }
        }

        {
            auto tt = control.cancellation_token();
            if (tt && tt->should_stop()) {
                control.record_interruption(InterruptionRecord{
                    .turns_completed = current_turn_,
                    .tools_completed = static_cast<int>(results.size()),
                    .tools_remaining = static_cast<int>(calls.size()) - static_cast<int>(results.size()),
                    .interrupted_tool_name = call.name,
                    .interrupted_tool_call_id = call.id,
                });
                break;
            }
        }

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
    if (!config_.enable_compaction) return;

    int total_tokens = 0;
    for (auto& msg : session_history_) {
        total_tokens += static_cast<int>(msg.content.size() / 3.5);
    }

    // Microcompact is handled by ContextOptimizer during pipeline assembly.
    // Here we trigger LLM-based compaction when token pressure is high.
    if (total_tokens > config_.model_max_tokens * 0.75 && compactor_) {
        spdlog::info("Loop: triggering LLM compaction at {} tokens", total_tokens);
        int keep_recent = config_.max_turns * 2;
        auto result = compactor_->compact_history(session_history_, keep_recent).get();
        if (!result.summary.empty()) {
            Message summary_msg;
            summary_msg.role = "system";
            summary_msg.content = "[Previous conversation summary]\n" + result.summary;
            session_history_.insert(session_history_.begin(), summary_msg);
            control.record_compaction(static_cast<int>(result.replaced.size()));
        }
    }
}

} // namespace merak
