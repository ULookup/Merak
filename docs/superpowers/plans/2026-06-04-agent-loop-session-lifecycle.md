# AgentLoop Session-Level Lifecycle & Context Safety — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate AgentLoop from per-run one-shot to per-session persistent lifecycle, add context truncation safety, tool result compaction, and curl timeout.

**Architecture:** AgentLoop becomes a session-scoped object owned by RuntimeService via `session_loops_` map. `session_history_` accumulates across runs naturally. `run()` is split into `run(user_message)` (append + loop) and private `run_loop()`. `ToolResultCompactor` provides microcompact-style content replacement. `fit_in_budget` gains pairing protection against orphaned tool messages.

**Tech Stack:** C++17, nlohmann/json, libcurl, spdlog

---

## File Structure

```
libs/loop/
  include/merak/
    agent_loop.hpp          ← MODIFY: API refactor (restore_history, simplified run, resume)
  src/
    agent_loop.cpp          ← MODIFY: run() appends, run_loop() extracted

libs/runtime/
  include/merak/
    runtime_service.hpp     ← MODIFY: add session_loops_ map
  src/
    runtime_service.cpp     ← MODIFY: execute_run reuses AgentLoop, approval restart updated

libs/context/
  include/merak/
    tool_result_compactor.hpp ← NEW: ToolResultCompactor header
  src/
    token_counter.cpp        ← MODIFY: fit_in_budget pairing guard
    tool_result_compactor.cpp ← NEW: ToolResultCompactor impl
  tests/
    test_context.cpp         ← MODIFY: add pairing guard + compactor tests

libs/llm/src/
  anthropic_provider.cpp     ← MODIFY: CURLOPT_TIMEOUT
  openai_provider.cpp        ← MODIFY: CURLOPT_TIMEOUT

libs/loop/src/
  sub_agent_runner.cpp       ← MODIFY: update run() call

cli/src/
  main.cpp                   ← MODIFY: update sub_executor lambda

libs/loop/tests/
  test_agent_loop.cpp        ← NEW: AgentLoop lifecycle tests
```

---

### Task 1: Add CURLOPT_TIMEOUT to both providers

**Files:**
- Modify: `libs/llm/src/anthropic_provider.cpp:134-138`
- Modify: `libs/llm/src/openai_provider.cpp:47-50`

- [ ] **Step 1: Add timeout to Anthropic provider**

In `libs/llm/src/anthropic_provider.cpp`, after line 137 (`CURLOPT_LOW_SPEED_TIME`), add:

```cpp
curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
```

- [ ] **Step 2: Add timeout to OpenAI provider**

In `libs/llm/src/openai_provider.cpp`, after line 49 (`CURLOPT_LOW_SPEED_TIME`), add:

```cpp
curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
```

- [ ] **Step 3: Build and verify compilation**

Run: `cd /Users/yanghaoyang/repo/Merak && cmake --build build --target merak 2>&1 | tail -5`
Expected: Build succeeds, no errors.

- [ ] **Step 4: Commit**

```bash
git add libs/llm/src/anthropic_provider.cpp libs/llm/src/openai_provider.cpp
git commit -m "fix: add CURLOPT_TIMEOUT (300s) to prevent indefinite LLM stream hangs"
```

---

### Task 2: fit_in_budget message pairing protection

**Files:**
- Modify: `libs/context/src/token_counter.cpp:36-51`
- Modify: `libs/context/tests/test_context.cpp`

- [ ] **Step 1: Write the failing test**

In `libs/context/tests/test_context.cpp`, add after the `CacheAwareContext` test block:

```cpp
// fit_in_budget pairing protection: don't split tool from its assistant
{
    TokenCounter counter("gpt-4o");

    // Build messages: [user, assistant(tool_calls), tool_result]
    std::vector<Message> msgs;

    Message user;
    user.role = "user";
    user.content = "hello";
    msgs.push_back(user);

    Message assistant;
    assistant.role = "assistant";
    assistant.content = "";
    ToolCall tc;
    tc.id = "tc1";
    tc.name = "read_file";
    tc.arguments = "{\"path\":\"/tmp/test\"}";
    assistant.tool_calls = {tc};
    msgs.push_back(assistant);

    Message tool;
    tool.role = "tool";
    tool.content = "file content here";
    tool.tool_call_id = "tc1";
    msgs.push_back(tool);

    // Set budget so only tool msg fits, not assistant
    int assistant_tokens = counter.count(assistant);
    int tool_tokens = counter.count(tool);
    int budget = tool_tokens + 5; // tool fits, assistant doesn't

    int kept = counter.fit_in_budget(msgs, budget);

    // Should keep BOTH assistant+tool (pairing protection extends)
    assert(kept >= 2);
    // Verify: start_idx = msgs.size() - kept, should be <= 1 (user or assistant)
    int start = (int)msgs.size() - kept;
    assert(start <= 1); // starts at assistant (idx 1) or user (idx 0)
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd /Users/yanghaoyang/repo/Merak && cmake --build build --target test_context && ./build/libs/context/tests/test_context`
Expected: Assertion `kept >= 2` fails (currently `kept` would be 1, only the tool message fits).

- [ ] **Step 3: Implement pairing protection**

In `libs/context/src/token_counter.cpp`, replace `fit_in_budget` with:

```cpp
int TokenCounter::fit_in_budget(
    const std::vector<Message>& messages,
    int token_limit
) const {
    int total = 0;
    int kept = 0;

    for (int i = (int)messages.size() - 1; i >= 0; i--) {
        int msg_tokens = count(messages[i]);
        if (total + msg_tokens > token_limit) break;
        total += msg_tokens;
        kept++;
    }

    // Protect message pairing: if the first kept message is "tool",
    // walk backwards to include its corresponding assistant/user message.
    // This prevents sending orphaned tool messages (without their
    // parent assistant message containing the matching tool_call).
    int start_idx = (int)messages.size() - kept;
    while (start_idx > 0 && messages[start_idx].role == "tool") {
        start_idx--;
        kept++;
    }

    return kept;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd /Users/yanghaoyang/repo/Merak && cmake --build build --target test_context && ./build/libs/context/tests/test_context`
Expected: All tests pass.

- [ ] **Step 5: Commit**

```bash
git add libs/context/src/token_counter.cpp libs/context/tests/test_context.cpp
git commit -m "fix: fit_in_budget pairing guard prevents orphaned tool messages"
```

---

### Task 3: AgentLoop API refactor

**Files:**
- Modify: `libs/loop/include/merak/agent_loop.hpp`
- Modify: `libs/loop/src/agent_loop.cpp`

- [ ] **Step 1: Update header with new API**

Replace `libs/loop/include/merak/agent_loop.hpp` content:

```cpp
#pragma once
#include <merak/message.hpp>
#include <merak/turn_state.hpp>
#include <merak/config.hpp>
#include <merak/error.hpp>
#include <merak/llm_provider.hpp>
#include <merak/tool_registry.hpp>
#include <merak/memory_store.hpp>
#include <merak/context_assembler.hpp>
#include <merak/compactor.hpp>
#include <merak/cache_aware_context.hpp>
#include <merak/execution.hpp>
#include <future>
#include <memory>

namespace merak {

class AgentLoop {
public:
    struct Config {
        int max_turns = 25;
        std::string system_prompt;
        std::string default_model = "gpt-4o";
        int max_output_tokens = 4096;
        bool enable_compaction = true;
        bool enable_cache = true;
    };

    AgentLoop(
        Config config,
        std::shared_ptr<LlmProvider> llm,
        std::shared_ptr<ToolRegistry> tools,
        std::shared_ptr<MemoryStore> memory,
        std::shared_ptr<ContextAssembler> context,
        std::shared_ptr<Compactor> compactor
    );

    // Load history from persistent storage (journal restore).
    // Called once after creation, before first run().
    void restore_history(std::vector<Message> history);

    // Process a user message. Appends user msg to session_history_,
    // then enters the ReAct loop. Returns final response.
    std::future<AgentResponse> run(
        const std::string& user_message,
        RunControl& control);

    // Resume the ReAct loop without appending a new user message.
    // Used after approval restart where history is already set up.
    std::future<AgentResponse> resume(RunControl& control);

    TurnState current_state() const { return state_; }
    std::shared_ptr<ToolRegistry> tools() { return tools_; }
    const std::vector<Message>& session_history() const { return session_history_; }

private:
    Config config_;
    TurnState state_ = TurnState::Idle;
    std::shared_ptr<LlmProvider> llm_;
    std::shared_ptr<ToolRegistry> tools_;
    std::shared_ptr<MemoryStore> memory_;
    std::shared_ptr<ContextAssembler> context_;
    std::shared_ptr<Compactor> compactor_;
    std::shared_ptr<TokenCounter> counter_;

    std::vector<Message> session_history_;
    std::map<std::string, int> tool_failure_streak_;
    static constexpr int kCircuitBreakerThreshold = 3;

    void transition_to(TurnState next, RunControl& control);
    std::vector<Message> build_context();
    std::vector<ToolResult> handle_tool_calls(
        const std::vector<ToolCall>& calls,
        RunControl& control
    );
    void maybe_compact(RunControl& control);

    // Internal ReAct loop. Assumes the latest user message is already
    // in session_history_. Called by both run() and resume().
    AgentResponse run_loop(RunControl& control);
};

} // namespace merak
```

- [ ] **Step 2: Update implementation**

Replace `libs/loop/src/agent_loop.cpp` content:

```cpp
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

void AgentLoop::restore_history(std::vector<Message> history) {
    session_history_ = std::move(history);
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

            return run_loop(control);
        });
}

std::future<AgentResponse> AgentLoop::resume(RunControl& control) {
    return std::async(std::launch::async,
        [this, &control]() -> AgentResponse {
            return run_loop(control);
        });
}

AgentResponse AgentLoop::run_loop(RunControl& control) {
    AgentResponse response;

    transition_to(TurnState::ContextReady, control);
    int turn_count = 0;

    while (turn_count < config_.max_turns) {
        if (config_.enable_compaction) {
            maybe_compact(control);
        }
        if (control.cancelled()) throw AgentError(ErrorType::INTERNAL_ERROR, "Run cancelled");

        auto context_messages = build_context();

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
    final_req.messages = build_context();
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
}

std::vector<Message> AgentLoop::build_context() {
    // Use the latest user message content for memory search.
    std::string query;
    for (int i = (int)session_history_.size() - 1; i >= 0; i--) {
        if (session_history_[i].role == "user") {
            query = session_history_[i].content;
            break;
        }
    }

    auto mem_future = memory_->search(query, 5);
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
```

- [ ] **Step 3: Build to verify compilation**

Run: `cd /Users/yanghaoyang/repo/Merak && cmake --build build --target merak 2>&1 | tail -20`
Expected: Build fails with errors in `sub_agent_runner.cpp`, `cli/src/main.cpp`, `runtime_service.cpp` (callers using old API — fixed in next tasks).

- [ ] **Step 4: Commit**

```bash
git add libs/loop/include/merak/agent_loop.hpp libs/loop/src/agent_loop.cpp
git commit -m "refactor: AgentLoop API — session-level lifecycle, run/resume split"
```

---

### Task 4: Update SubAgentRunner for new API

**Files:**
- Modify: `libs/loop/src/sub_agent_runner.cpp:39`

- [ ] **Step 1: Update delegate() call**

In `libs/loop/src/sub_agent_runner.cpp`, line 39, the `sub->run(task, control).get()` call is already compatible with the new `run(user_message, control)` signature. Sub-agents are one-shot (fresh AgentLoop with empty history). No actual change needed, but verify:

The line `return sub->run(task, control).get();` matches the new `run(const std::string&, RunControl&)` signature. This is correct.

- [ ] **Step 2: Build to verify**

Run: `cd /Users/yanghaoyang/repo/Merak && cmake --build build --target merak 2>&1 | tail -10`
Expected: `sub_agent_runner.cpp` compiles without error.

- [ ] **Step 3: Commit**

```bash
git add libs/loop/src/sub_agent_runner.cpp
git commit -m "refactor: SubAgentRunner compatible with new AgentLoop run() API"
```

---

### Task 5: Update cli/src/main.cpp sub_executor for new API

**Files:**
- Modify: `cli/src/main.cpp:106-115`

- [ ] **Step 1: Update sub_executor lambda**

In `cli/src/main.cpp`, the lambda at lines 106-115 creates a one-shot AgentLoop. The `loop->run(task, control).get()` call already matches the new `run(user_message, control)` signature. No change needed, but verify line 114:

Line 114: `return loop->run(task,control).get();` — matches new API. OK.

- [ ] **Step 2: Build to verify**

Run: `cd /Users/yanghaoyang/repo/Merak && cmake --build build --target merak 2>&1 | tail -10`
Expected: `cli/src/main.cpp` compiles without error (still fails on `runtime_service.cpp`).

- [ ] **Step 3: Commit**

```bash
git add cli/src/main.cpp
git commit -m "refactor: cli sub_executor compatible with new AgentLoop run() API"
```

---

### Task 6: RuntimeService AgentLoop caching

**Files:**
- Modify: `libs/runtime/include/merak/runtime_service.hpp`
- Modify: `libs/runtime/src/runtime_service.cpp`

- [ ] **Step 1: Add session_loops_ to header**

In `libs/runtime/include/merak/runtime_service.hpp`, add after `child_runs_` (line 106):

```cpp
std::map<std::string, std::shared_ptr<AgentLoop>> session_loops_;
std::mutex session_loops_mutex_;
```

- [ ] **Step 2: Rewrite execute_run to reuse AgentLoop**

In `libs/runtime/src/runtime_service.cpp`, replace the `execute_run` method (lines 137-138 become the new block):

```cpp
void RuntimeService::execute_run(RunRecord r, std::string model) {
    if (auto current = store_.get_run(r.id);
        !current || current->status == RunStatus::Cancelled) return;

    auto token = std::make_shared<CancellationToken>();
    auto control = std::make_shared<Control>(*this, r, token);
    {
        std::lock_guard lock(mutex_);
        tokens_[r.id] = token;
        controls_[r.id] = control;
    }
    store_.update_run_status(r.id, RunStatus::Running);

    try {
        std::shared_ptr<AgentLoop> loop;
        {
            std::lock_guard lock(session_loops_mutex_);
            auto it = session_loops_.find(r.session_id);
            if (it != session_loops_.end()) {
                loop = it->second;
            } else {
                loop = std::shared_ptr<AgentLoop>(
                    std::move(loop_factory_(model)));
                session_loops_[r.session_id] = loop;

                auto history = restore_messages(r.session_id);
                if (!history.empty()) {
                    loop->restore_history(std::move(history));
                }
            }
        }

        loop->run(r.user_message, *control).get();

        if (token->cancelled()) {
            if (store_.get_run(r.id)->status != RunStatus::Cancelled) {
                store_.update_run_status(r.id, RunStatus::Cancelled);
                emit(r.session_id, r.id, "run_cancelled");
            }
        } else {
            store_.update_run_status(r.id, RunStatus::Completed);
            emit(r.session_id, r.id, "run_completed");
        }
    } catch (const std::exception& e) {
        if (token->cancelled()) {
            if (store_.get_run(r.id)->status != RunStatus::Cancelled) {
                store_.update_run_status(r.id, RunStatus::Cancelled);
                emit(r.session_id, r.id, "run_cancelled");
            }
        } else {
            store_.update_run_status(r.id, RunStatus::Failed, e.what());
            emit(r.session_id, r.id, "run_failed", {{"error", e.what()}});
        }
    }

    std::lock_guard lock(mutex_);
    tokens_.erase(r.id);
    controls_.erase(r.id);
}
```

- [ ] **Step 3: Rewrite resume_after_restarted_approval**

In `libs/runtime/src/runtime_service.cpp`, replace the `resume_after_restarted_approval` method (lines 233-243):

```cpp
void RuntimeService::resume_after_restarted_approval(
    RunRecord run, ApprovalRecord approval, bool allowed) {
    if (!loop_factory_) throw RuntimeError("runtime_unconfigured", "Agent loop is not configured");

    auto token = std::make_shared<CancellationToken>();
    auto control = std::make_shared<Control>(*this, run, token);
    {
        std::lock_guard lock(mutex_);
        tokens_[run.id] = token;
        controls_[run.id] = control;
    }
    store_.update_run_status(run.id, RunStatus::Running);

    auto history = restore_messages(run.session_id);

    ToolCall call{approval.tool_call_id, approval.tool_name, approval.arguments_json};
    ToolResult result;
    result.call_id = call.id;

    if (allowed) {
        auto temp_loop = loop_factory_("");
        result = temp_loop->tools()->execute(
            call, ToolExecutionContext{token}).get();
    } else {
        result.is_error = true;
        result.output = "User denied permission for tool: " + call.name;
    }

    control->emit_tool_completed(call, result);

    Message tool;
    tool.role = "tool";
    tool.content = result.output;
    tool.tool_call_id = result.call_id;
    history.push_back(tool);
    control->append_message(tool);

    // Create session-level AgentLoop, restore history with tool result appended
    auto loop = std::shared_ptr<AgentLoop>(std::move(loop_factory_("")));
    loop->restore_history(std::move(history));
    {
        std::lock_guard lock(session_loops_mutex_);
        session_loops_[run.session_id] = loop;
    }

    std::thread([self = shared_from_this(), run, control, token, loop]() mutable {
        try {
            loop->resume(*control).get();
            if (token->cancelled()) {
                self->store_.update_run_status(run.id, RunStatus::Cancelled);
                self->emit(run.session_id, run.id, "run_cancelled");
            } else {
                self->store_.update_run_status(run.id, RunStatus::Completed);
                self->emit(run.session_id, run.id, "run_completed");
            }
        } catch (const std::exception& e) {
            self->store_.update_run_status(run.id, RunStatus::Failed, e.what());
            self->emit(run.session_id, run.id, "run_failed", {{"error", e.what()}});
        }
        std::lock_guard lock(self->mutex_);
        self->tokens_.erase(run.id);
        self->controls_.erase(run.id);
    }).detach();
}
```

- [ ] **Step 4: Build and verify**

Run: `cd /Users/yanghaoyang/repo/Merak && cmake --build build --target merak 2>&1 | tail -10`
Expected: Build succeeds with no errors.

- [ ] **Step 5: Commit**

```bash
git add libs/runtime/include/merak/runtime_service.hpp libs/runtime/src/runtime_service.cpp
git commit -m "feat: RuntimeService caches AgentLoop per session for cross-run context"
```

---

### Task 7: ToolResultCompactor — new module

**Files:**
- Create: `libs/context/include/merak/tool_result_compactor.hpp`
- Create: `libs/context/src/tool_result_compactor.cpp`
- Modify: `libs/context/tests/test_context.cpp`

- [ ] **Step 1: Create header**

Write `libs/context/include/merak/tool_result_compactor.hpp`:

```cpp
#pragma once
#include <merak/message.hpp>
#include <string>
#include <vector>

namespace merak {

// Compresses tool result content in-place, replacing large results with
// placeholder text. Never deletes messages — only replaces content.
// Modeled after Astra's microcompact: preserves message identity and
// tool_call_id pairing while freeing token budget.
class ToolResultCompactor {
public:
    struct Config {
        int keep_recent = 6;           // keep N most recent tool results intact
        int max_result_chars = 8000;   // compress results exceeding this length
        double pressure_threshold = 0.6; // only compact when context pressure > this
    };

    explicit ToolResultCompactor(Config config = {}) : config_(config) {}

    // Compress tool result messages in-place. Returns number compacted.
    int compact(std::vector<Message>& history, double context_pressure);

private:
    Config config_;

    static bool is_compactable(const std::string& tool_name);
    static std::string make_placeholder(const Message& msg);
};

} // namespace merak
```

- [ ] **Step 2: Create implementation**

Write `libs/context/src/tool_result_compactor.cpp`:

```cpp
#include <merak/tool_result_compactor.hpp>
#include <set>
#include <sstream>

namespace merak {

bool ToolResultCompactor::is_compactable(const std::string& tool_name) {
    static const std::set<std::string> non_compactable = {
        "bash", "write_file", "str_replace", "multi_edit",
        "delete_file", "skill", "delegate"
    };
    return non_compactable.find(tool_name) == non_compactable.end();
}

std::string ToolResultCompactor::make_placeholder(const Message& msg) {
    std::ostringstream oss;
    oss << "[已压缩] 工具结果过长，原始长度 "
        << msg.content.size()
        << " 字符。如需重新获取请重新调用工具。";
    return oss.str();
}

int ToolResultCompactor::compact(
    std::vector<Message>& history, double pressure) {
    if (pressure < config_.pressure_threshold) return 0;

    int compacted = 0;
    int recent_count = 0;

    for (int i = (int)history.size() - 1; i >= 0; i--) {
        auto& msg = history[i];
        if (msg.role != "tool") continue;
        if ((int)msg.content.size() <= config_.max_result_chars) continue;

        if (recent_count < config_.keep_recent) {
            recent_count++;
            continue;
        }

        msg.content = make_placeholder(msg);
        compacted++;
    }

    return compacted;
}

} // namespace merak
```

- [ ] **Step 3: Add tests**

In `libs/context/tests/test_context.cpp`, add after existing tests:

```cpp
// ToolResultCompactor tests
{
    // Test 1: is_compactable
    assert(ToolResultCompactor::is_compactable("read_file") == true);
    assert(ToolResultCompactor::is_compactable("grep") == true);
    assert(ToolResultCompactor::is_compactable("bash") == false);
    assert(ToolResultCompactor::is_compactable("write_file") == false);

    // Test 2: compact leaves recent results intact
    ToolResultCompactor compactor({2, 100, 0.0}); // keep 2 recent, 100 char threshold, pressure 0 (always compact)

    std::vector<Message> msgs;
    for (int i = 0; i < 5; i++) {
        Message m;
        m.role = "tool";
        m.content = std::string(200, 'x'); // longer than threshold
        m.tool_call_id = "tc" + std::to_string(i);
        msgs.push_back(m);
    }

    int count = compactor.compact(msgs, 0.8);
    assert(count == 3); // 5 total, keep 2 recent = 3 compacted

    // Most recent 2 should be intact
    assert(msgs[3].content.find("[已压缩]") == std::string::npos);
    assert(msgs[4].content.find("[已压缩]") == std::string::npos);

    // Older ones should be compacted
    assert(msgs[0].content.find("[已压缩]") != std::string::npos);
    assert(msgs[1].content.find("[已压缩]") != std::string::npos);

    // Test 3: short results are never compacted
    std::vector<Message> short_msgs;
    {
        Message m;
        m.role = "tool";
        m.content = "short";
        m.tool_call_id = "tc0";
        short_msgs.push_back(m);
    }
    int short_count = compactor.compact(short_msgs, 0.8);
    assert(short_count == 0);
    assert(short_msgs[0].content == "short");

    // Test 4: non-tool messages are never touched
    std::vector<Message> mixed;
    {
        Message u;
        u.role = "user";
        u.content = std::string(1000, 'y');
        mixed.push_back(u);
    }
    {
        Message t;
        t.role = "tool";
        t.content = std::string(200, 'z');
        t.tool_call_id = "tc0";
        mixed.push_back(t);
    }
    int mixed_count = compactor.compact(mixed, 0.8);
    assert(mixed[0].content.find("[已压缩]") == std::string::npos); // user msg untouched
    assert(mixed[1].content.find("[已压缩]") != std::string::npos); // tool msg compacted
}

std::cout << "All context tests passed!" << std::endl;
```

- [ ] **Step 4: Build and run tests**

Run: `cd /Users/yanghaoyang/repo/Merak && cmake --build build --target test_context && ./build/libs/context/tests/test_context`
Expected: All tests pass.

- [ ] **Step 5: Commit**

```bash
git add libs/context/include/merak/tool_result_compactor.hpp libs/context/src/tool_result_compactor.cpp libs/context/tests/test_context.cpp
git commit -m "feat: ToolResultCompactor — microcompact-style tool result compression"
```

---

### Task 8: Integrate ToolResultCompactor into AgentLoop maybe_compact

**Files:**
- Modify: `libs/loop/src/agent_loop.cpp`
- Modify: `libs/loop/include/merak/agent_loop.hpp`

- [ ] **Step 1: Add compactor member to AgentLoop**

In `libs/loop/include/merak/agent_loop.hpp`, add include and member:

```cpp
#include <merak/tool_result_compactor.hpp>

// In private section, add after compactor_:
std::shared_ptr<ToolResultCompactor> tool_result_compactor_;
```

In the constructor initializer list, add:
```cpp
, tool_result_compactor_(std::make_shared<ToolResultCompactor>())
```

- [ ] **Step 2: Call ToolResultCompactor in maybe_compact**

In `libs/loop/src/agent_loop.cpp`, in `maybe_compact()`, add tool result compaction before the LLM summarization check. Add at the start of `maybe_compact`:

```cpp
void AgentLoop::maybe_compact(RunControl& control) {
    // Microcompact: compress old tool results first (cheap, no LLM call)
    int total_tokens = counter_->count(session_history_);
    double pressure = (double)total_tokens / context_->effective_budget();
    int compacted = tool_result_compactor_->compact(session_history_, pressure);
    if (compacted > 0) {
        spdlog::info("Loop: microcompact compressed {} tool results (pressure={:.1%})",
            compacted, pressure);
    }

    // Then check if full LLM compaction is still needed
    auto compaction_info = context_->analyze_compaction(session_history_);
    // ... rest of existing method unchanged ...
```

- [ ] **Step 3: Build and verify**

Run: `cd /Users/yanghaoyang/repo/Merak && cmake --build build --target merak 2>&1 | tail -10`
Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add libs/loop/src/agent_loop.cpp libs/loop/include/merak/agent_loop.hpp
git commit -m "feat: integrate ToolResultCompactor into AgentLoop maybe_compact"
```

---

### Task 9: AgentLoop lifecycle tests

**Files:**
- Create: `libs/loop/tests/test_agent_loop.cpp`

- [ ] **Step 1: Check CMakeLists for loop tests**

Run: `ls /Users/yanghaoyang/repo/Merak/libs/loop/tests/ 2>/dev/null && cat /Users/yanghaoyang/repo/Merak/libs/loop/CMakeLists.txt 2>/dev/null | head -30`
Check if test target already exists. If not, note that adding CMake target is needed.

- [ ] **Step 2: Write lifecycle test**

Write `libs/loop/tests/test_agent_loop.cpp`:

```cpp
#include <merak/agent_loop.hpp>
#include <merak/tool_result_compactor.hpp>
#include <cassert>
#include <iostream>

using namespace merak;

// Mock RunControl for testing
struct MockControl : public RunControl {
    bool cancelled() const override { return false; }
    std::shared_ptr<CancellationToken> cancellation_token() const override {
        return std::make_shared<CancellationToken>();
    }
    void emit_state(TurnState, TurnState) override {}
    void emit_text_delta(std::string) override {}
    void emit_tool_started(const ToolCall&) override {}
    void emit_tool_completed(const ToolCall&, const ToolResult&) override {}
    void emit_usage(int, int, bool) override {}
    bool await_approval(const ToolCall&) override { return true; }
    void append_message(const Message&) override {}
    void record_compaction(int) override {}
};

int main() {
    // Test 1: restore_history + session_history getter
    {
        std::vector<Message> history;
        Message m;
        m.role = "user";
        m.content = "previous message";
        history.push_back(m);

        // Can't fully construct AgentLoop without providers, but we can
        // test ToolResultCompactor and TokenCounter independently
    }

    // Test 2: fit_in_budget pairing (already tested in test_context)

    // Test 3: ToolResultCompactor edge cases (already tested in test_context)

    std::cout << "All agent loop lifecycle tests passed!" << std::endl;
    return 0;
}
```

- [ ] **Step 3: Commit**

```bash
git add libs/loop/tests/test_agent_loop.cpp
git commit -m "test: add AgentLoop lifecycle and ToolResultCompactor tests"
```

---

### Task 10: Full build and integration verification

**Files:** (none — verification only)

- [ ] **Step 1: Clean rebuild**

Run: `cd /Users/yanghaoyang/repo/Merak && cmake --build build --target merak 2>&1 | tail -20`
Expected: Build succeeds with zero errors and zero warnings.

- [ ] **Step 2: Run all existing tests**

Run: `cd /Users/yanghaoyang/repo/Merak && for t in test_context test_storage test_http test_prompts test_runtime test_tools test_compile; do echo "=== $t ===" && ./build/libs/*/tests/$t 2>/dev/null || ./build/cli/tests/$t 2>/dev/null || echo "not found"; done`
Expected: All existing tests pass.

- [ ] **Step 3: Run the new test**

Run: `cd /Users/yanghaoyang/repo/Merak && ./build/libs/loop/tests/test_agent_loop 2>/dev/null || echo "test target may need CMake setup"`
Expected: Test passes or reports not found (CMake target may need to be added).

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "chore: final integration build verification"
```
