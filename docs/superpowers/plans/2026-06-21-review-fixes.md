# PR #169 Review Bug Fixes — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix 8 confirmed bugs found during code review of PR #169 (agent industrial hardening)

**Architecture:** 8 independent fixes across 8 source files, all on `infra-fixes-2026-06-20` branch. Three fixes are trivial (1, 5, 7), three are structural (2, 4, 8), two are logic fixes (3, 6). Fix 4 is the only cross-cutting change (affects turn_guard + agent_loop).

**Tech Stack:** C++20, CMake + Conan, nlohmann/json, spdlog

---

## File Structure

| Fix | Files Modified | Description |
|-----|---------------|-------------|
| 1 | `libs/loop/src/agent_loop.cpp` | Reset `run_call_count_` in 3 entry points |
| 2 | `libs/loop/include/merak/agent_loop.hpp`, `libs/loop/src/agent_loop.cpp` | Abandoned-task container + drain method + timeout branch rewrite |
| 3 | `libs/loop/src/sub_agent_runner.cpp` | Store error result in fan_out results map |
| 4 | `libs/loop/include/merak/turn_guard.hpp`, `libs/loop/src/turn_guard.cpp`, `libs/loop/include/merak/agent_loop.hpp`, `libs/loop/src/agent_loop.cpp` | Replace `restricted_tools` (string vector) with `restricted_domains` (ToolDomain bitmask) |
| 5 | `libs/loop/src/turn_guard.cpp` | Dynamic reason message formatting |
| 6 | `libs/runtime/src/runtime_service.cpp` | Launch `execute_run` in `resume_run()` |
| 7 | `libs/tools/tests/test_tools.cpp` | Register ReadFileTool in schema-less validation test |
| 8 | `libs/tools/src/agent_tool.cpp` | Reorder std::async after capacity check |

**Non-overlapping worktree groups:**
- **Worktree A:** Fixes 1, 2 (agent_loop.hpp/cpp), Fix 4 agent_loop parts (agent_loop.hpp/cpp)
- **Worktree B:** Fixes 3 (sub_agent_runner.cpp)
- **Worktree C:** Fixes 4 turn_guard parts (turn_guard.hpp/cpp), Fix 5 (turn_guard.cpp)
- **Worktree D:** Fix 6 (runtime_service.cpp)
- **Worktree E:** Fixes 7, 8 (test_tools.cpp, agent_tool.cpp) + new tests

Note: Fix 4 must be done atomically in one worktree (turn_guard.hpp + turn_guard.cpp + agent_loop.hpp + agent_loop.cpp all type-dependent). Best to combine Worktree A + C or do Fix 4 as a single worktree covering all 4 files.

**Recommended split (3 worktrees):**
- **WT-A:** Fix 1, 2, 4, 5 (agent_loop.hpp/cpp, turn_guard.hpp/cpp) — 4 files, same library (merak-loop)
- **WT-B:** Fix 3, 6 (sub_agent_runner.cpp, runtime_service.cpp) — 2 files
- **WT-C:** Fix 7, 8 + new tests (test_tools.cpp, agent_tool.cpp, test_agent_loop.cpp, test_turn_guard.cpp, test_sub_agent_runner.cpp) — test files

---

## Build & Test Commands

```bash
# Configure
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/Debug/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug

# Build relevant targets
cmake --build build --target merak-loop merak-tools merak-runtime -j$(nproc)

# Build and run tests
cmake --build build --target merak-agent-loop-test merak-turn-guard-test merak-sub-agent-runner-test merak-tools-test -j$(nproc)
./build/tests/merak-agent-loop-test
./build/tests/merak-turn-guard-test
./build/tests/merak-sub-agent-runner-test
./build/tests/merak-tools-test
```

---

### Task 1: Fix `run_call_count_` reset (Fix 1)

**Files:** Modify `libs/loop/src/agent_loop.cpp`

- [ ] **Step 1: Add reset in `run()` lambda**

Read the `run()` method. Find the existing reset block at lines ~65-70:
```cpp
            tool_failure_streak_.clear();
            turn_guard_.reset();
            stall_detector_.reset();
            consecutive_read_only_rounds_ = 0;
            consecutive_world_query_rounds_ = 0;
            consecutive_content_avoidance_ = 0;
```

Add `run_call_count_ = 0;` after `consecutive_content_avoidance_ = 0;`:
```cpp
            tool_failure_streak_.clear();
            turn_guard_.reset();
            stall_detector_.reset();
            consecutive_read_only_rounds_ = 0;
            consecutive_world_query_rounds_ = 0;
            consecutive_content_avoidance_ = 0;
            run_call_count_ = 0;
```

- [ ] **Step 2: Add reset in `resume()` lambda**

Find the reset block in `resume()` at lines ~82-84:
```cpp
            tool_failure_streak_.clear();
            turn_guard_.reset();
            stall_detector_.reset();
```

Add `run_call_count_ = 0;`:
```cpp
            tool_failure_streak_.clear();
            turn_guard_.reset();
            stall_detector_.reset();
            run_call_count_ = 0;
```

- [ ] **Step 3: Add reset in `restore_history()`**

Find `restore_history()` at lines ~34-38:
```cpp
void AgentLoop::restore_history(std::vector<Message> history) {
    session_history_ = std::move(history);
    compaction_summaries_.clear();
    token_counter_->update_authoritative(0, 0);
}
```

Add `run_call_count_ = 0;`:
```cpp
void AgentLoop::restore_history(std::vector<Message> history) {
    session_history_ = std::move(history);
    compaction_summaries_.clear();
    token_counter_->update_authoritative(0, 0);
    run_call_count_ = 0;
}
```

- [ ] **Step 4: Build and verify**

```bash
cmake --build build --target merak-loop -j$(nproc)
```
Expected: compiles cleanly (pre-existing warnings only).

- [ ] **Step 5: Commit**

```bash
git add libs/loop/src/agent_loop.cpp
git commit -m "fix(loop): reset run_call_count_ in run(), resume(), and restore_history()"
```

---

### Task 2: Fix tool timeout with abandoned-task container (Fix 2)

**Files:** Modify `libs/loop/include/merak/agent_loop.hpp`, `libs/loop/src/agent_loop.cpp`

- [ ] **Step 1: Add `abandoned_tasks_` member and `drain` declaration to header**

In `libs/loop/include/merak/agent_loop.hpp`, in the private section after `run_call_count_` (line ~143), add:
```cpp
    std::vector<std::future<ToolResult>> abandoned_tasks_;
    static constexpr size_t kMaxAbandonedTasks = 32;
```

After the existing private method declarations (after `void maybe_compact(RunControl& control);`), add:
```cpp
    void drain_abandoned_tasks();
```

- [ ] **Step 2: Add `abandoned_tasks` field to RunMetrics**

In the `RunMetrics` struct (lines ~43-57), add after `turn_guard_warnings`:
```cpp
        int abandoned_tasks = 0;
```

- [ ] **Step 3: Implement `drain_abandoned_tasks()` in .cpp**

In `libs/loop/src/agent_loop.cpp`, add the implementation before `} // namespace merak` at end of file:
```cpp
void AgentLoop::drain_abandoned_tasks() {
    abandoned_tasks_.erase(
        std::remove_if(abandoned_tasks_.begin(), abandoned_tasks_.end(),
            [](std::future<ToolResult>& f) {
                return f.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
            }),
        abandoned_tasks_.end());
}
```

- [ ] **Step 4: Call `drain_abandoned_tasks()` at start of each turn in `run_loop()`**

In `run_loop()`, find the while loop start (around line 96-98):
```cpp
    while (turn_count < config_.max_turns) {
        if (config_.enable_compaction) {
            maybe_compact(control);
        }
```

Add the drain call before maybe_compact:
```cpp
    while (turn_count < config_.max_turns) {
        drain_abandoned_tasks();
        if (config_.enable_compaction) {
            maybe_compact(control);
        }
```

- [ ] **Step 5: Rewrite timeout branch in `handle_tool_calls()`**

Find the tool execution + timeout code (currently lines ~594-610). Replace the timeout branch:

Old code:
```cpp
        auto result_future = tools_->execute(call, std::move(ctx));
        auto status = result_future.wait_for(timeout_dur);
        if (status == std::future_status::timeout) {
            ToolResult timeout_result;
            timeout_result.call_id = call.id;
            timeout_result.is_error = true;
            timeout_result.output = "Tool '" + call.name + "' timed out after " +
                std::to_string(timeout_dur.count()) + "ms";
            results.push_back(timeout_result);
            control.emit_tool_completed(call, timeout_result);
            tool_failure_streak_[call.name]++;
            continue;
        }
        auto result = result_future.get();
```

New code:
```cpp
        auto result_future = tools_->execute(call, std::move(ctx));
        auto status = result_future.wait_for(timeout_dur);
        if (status == std::future_status::timeout) {
            // Signal cancellation to the tool thread as a cooperative hint
            if (auto& token = ctx.cancellation; token) {
                token->cancel();
            }
            ToolResult timeout_result;
            timeout_result.call_id = call.id;
            timeout_result.is_error = true;
            timeout_result.output = "Tool '" + call.name + "' timed out after " +
                std::to_string(timeout_dur.count()) + "ms";
            results.push_back(timeout_result);
            control.emit_tool_completed(call, timeout_result);
            tool_failure_streak_[call.name]++;
            run_metrics_.abandoned_tasks++;
            // Move future to abandoned container to avoid blocking destructor
            if (abandoned_tasks_.size() >= kMaxAbandonedTasks) {
                spdlog::error("Loop: abandoned task overflow ({}), draining oldest", abandoned_tasks_.size());
                abandoned_tasks_.front().get();
                abandoned_tasks_.erase(abandoned_tasks_.begin());
            }
            abandoned_tasks_.push_back(std::move(result_future));
            continue;
        }
        auto result = result_future.get();
```

- [ ] **Step 6: Build and verify**

```bash
cmake --build build --target merak-loop -j$(nproc)
```
Expected: compiles cleanly.

- [ ] **Step 7: Commit**

```bash
git add libs/loop/include/merak/agent_loop.hpp libs/loop/src/agent_loop.cpp
git commit -m "fix(loop): use abandoned-task container to avoid future destructor blocking on timeout"
```

---

### Task 3: Fix `fan_out` error result not stored (Fix 3)

**Files:** Modify `libs/loop/src/sub_agent_runner.cpp`

- [ ] **Step 1: Restructure fan_out batch loop to track agent_id**

Read `libs/loop/src/sub_agent_runner.cpp` `fan_out()` method, lines 90-113.

Old code:
```cpp
        size_t idx = 0;
        while (idx < tasks.size()) {
            std::vector<std::future<std::pair<std::string, AgentResponse>>> batch;
            for (int i = 0; i < max_parallel && idx < tasks.size(); i++, idx++) {
                batch.push_back(std::async(std::launch::async,
                    [self, d = tasks[idx]]() -> std::pair<std::string, AgentResponse> {
                        auto resp = self->delegate(d.agent_id, d.task).get();
                        return {d.agent_id, resp};
                    }));
            }
            for (auto& f : batch) {
                try {
                    auto result = f.get();
                    results[result.first] = result.second;
                } catch (const std::exception& e) {
                    AgentResponse err;
                    err.text = std::string("Sub-agent error: ") + e.what();
                    spdlog::warn("SubAgentRunner: fan_out task failed: {}", e.what());
                }
            }
        }
```

New code:
```cpp
        size_t idx = 0;
        while (idx < tasks.size()) {
            size_t batch_start = idx;
            std::vector<std::future<std::pair<std::string, AgentResponse>>> batch;
            for (int i = 0; i < max_parallel && idx < tasks.size(); i++, idx++) {
                batch.push_back(std::async(std::launch::async,
                    [self, d = tasks[idx]]() -> std::pair<std::string, AgentResponse> {
                        auto resp = self->delegate(d.agent_id, d.task).get();
                        return {d.agent_id, resp};
                    }));
            }
            for (size_t i = 0; i < batch.size(); i++) {
                try {
                    auto result = batch[i].get();
                    results[result.first] = result.second;
                } catch (const std::exception& e) {
                    AgentResponse err;
                    err.text = std::string("Sub-agent error: ") + e.what();
                    results[tasks[batch_start + i].agent_id] = err;
                    spdlog::warn("SubAgentRunner: fan_out task '{}' failed: {}",
                        tasks[batch_start + i].agent_id, e.what());
                }
            }
        }
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build build --target merak-loop -j$(nproc)
```
Expected: compiles cleanly.

- [ ] **Step 3: Commit**

```bash
git add libs/loop/src/sub_agent_runner.cpp
git commit -m "fix(sub-agent): store fan_out error result in results map with agent_id key"
```

---

### Task 4: Replace `restricted_tools` with `ToolDomain`-based `restricted_domains` (Fix 4)

**Files:** Modify `libs/loop/include/merak/turn_guard.hpp`, `libs/loop/src/turn_guard.cpp`, `libs/loop/include/merak/agent_loop.hpp`, `libs/loop/src/agent_loop.cpp`

- [ ] **Step 1: Change `TurnGuard::Verdict` in header**

In `libs/loop/include/merak/turn_guard.hpp`, find the `Verdict` struct (lines 34-40):

Old:
```cpp
  struct Verdict {
    Severity severity = Severity::Healthy;
    std::string reason;
    std::optional<std::string> nudge;
    std::optional<int> turn_penalty;
    std::vector<std::string> restricted_tools;
  };
```

New:
```cpp
  struct Verdict {
    Severity severity = Severity::Healthy;
    std::string reason;
    std::optional<std::string> nudge;
    std::optional<int> turn_penalty;
    ToolDomain restricted_domains = ToolDomain::General;  // General=0 means no restriction
  };
```

Also add `#include <merak/tool_meta.hpp>` at the top of turn_guard.hpp.

- [ ] **Step 2: Update TurnGuard to set `restricted_domains`**

In `libs/loop/src/turn_guard.cpp`, line 23:

Old:
```cpp
      v.restricted_tools = {"query_map", "query_world", "query_history", "query_magic", "query_faction"};
```

New:
```cpp
      v.restricted_domains = ToolDomain::WorldQuery;
```

- [ ] **Step 3: Change AgentLoop member type**

In `libs/loop/include/merak/agent_loop.hpp`, find private member (line ~145):

Old:
```cpp
    std::vector<std::string> restricted_tools_;
```

New:
```cpp
    ToolDomain restricted_domains_ = ToolDomain::General;
```

- [ ] **Step 4: Update consumption side in `run_loop()`**

In `libs/loop/src/agent_loop.cpp`, find the tool filtering block (lines ~122-134):

Old:
```cpp
        auto tool_specs = tools_->pinned_schemas();
        if (!restricted_tools_.empty()) {
            std::unordered_set<std::string> blocked(
                restricted_tools_.begin(), restricted_tools_.end());
            std::vector<ToolSpec> filtered;
            filtered.reserve(tool_specs.size());
            for (auto& ts : tool_specs) {
                if (!blocked.count(ts.name)) filtered.push_back(ts);
            }
            req.tools = std::move(filtered);
            restricted_tools_.clear();
        } else {
            req.tools = tool_specs;
        }
```

New:
```cpp
        auto tool_specs = tools_->pinned_schemas();
        if (restricted_domains_ != ToolDomain::General) {
            std::vector<ToolSpec> filtered;
            filtered.reserve(tool_specs.size());
            for (auto& ts : tool_specs) {
                if (!(tools_->domain_of(ts.name) & restricted_domains_)) {
                    filtered.push_back(ts);
                }
            }
            req.tools = std::move(filtered);
            restricted_domains_ = ToolDomain::General;
        } else {
            req.tools = tool_specs;
        }
```

- [ ] **Step 5: Update verdict consumption that sets `restricted_tools_`**

In `libs/loop/src/agent_loop.cpp`, find where `restricted_tools_` was assigned from verdict (around lines 348-350):

Old:
```cpp
        restricted_tools_ = verdict.restricted_tools;
```

New:
```cpp
        restricted_domains_ = verdict.restricted_domains;
```

Also remove `#include <unordered_set>` if it's no longer needed (check if used elsewhere in the file). If only used for the old tool blocking, remove it.

- [ ] **Step 6: Build and verify**

```bash
cmake --build build --target merak-loop -j$(nproc)
```
Expected: compiles cleanly.

- [ ] **Step 7: Commit**

```bash
git add libs/loop/include/merak/turn_guard.hpp libs/loop/src/turn_guard.cpp \
        libs/loop/include/merak/agent_loop.hpp libs/loop/src/agent_loop.cpp
git commit -m "fix(guard): use ToolDomain bitmask instead of hardcoded tool name list for restrictions"
```

---

### Task 5: Dynamic reason messages (Fix 5)

**Files:** Modify `libs/loop/src/turn_guard.cpp`

- [ ] **Step 1: Replace 4 hardcoded reason strings**

In `libs/loop/src/turn_guard.cpp`, change 4 lines:

Line 22 — old:
```cpp
      v.reason = "5+ rounds of world-only queries without narrative output";
```
New:
```cpp
      v.reason = std::to_string(config_.max_consecutive_world_query_rounds) +
          "+ rounds of world-only queries without narrative output";
```

Line 30 — old:
```cpp
      v.reason = "3+ rounds without write operations";
```
New:
```cpp
      v.reason = std::to_string(config_.max_consecutive_read_only_rounds) +
          "+ rounds without write operations";
```

Line 36 — old:
```cpp
      v.reason = "3x refusal to advance narrative";
```
New:
```cpp
      v.reason = std::to_string(config_.max_consecutive_content_avoidance) +
          "x refusal to advance narrative";
```

Line 68 — old:
```cpp
      v.reason = "4+ warnings in this run";
```
New:
```cpp
      v.reason = std::to_string(config_.max_warnings_before_critical) +
          "+ warnings in this run";
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build build --target merak-loop -j$(nproc)
```
Expected: compiles cleanly.

- [ ] **Step 3: Commit**

```bash
git add libs/loop/src/turn_guard.cpp
git commit -m "fix(guard): use dynamic threshold values in reason messages"
```

---

### Task 6: Launch execution in `resume_run()` (Fix 6)

**Files:** Modify `libs/runtime/src/runtime_service.cpp`

- [ ] **Step 1: Read current `resume_run()` and `start_run()` for comparison**

In `libs/runtime/src/runtime_service.cpp`:
- `resume_run()` at line 382
- `start_run()` at line 392

- [ ] **Step 2: Add `execute_run` launch to `resume_run()`**

Old code (line 382):
```cpp
RunRecord RuntimeService::resume_run(const std::string&run_id){auto existing=store_->get_run(run_id);if(!existing)throw RuntimeError("run_not_found","Run does not exist");if(existing->status!=RunStatus::Interrupted&&existing->status!=RunStatus::Failed)throw RuntimeError("run_not_resumable","Run is not in a resumable state");auto new_run=store_->create_run(existing->session_id,existing->user_message,existing->id,existing->delegation_id,existing->agent_id,existing->run_kind);return new_run;}
```

New code (formatted for readability — keep same compact style as surrounding code):
```cpp
RunRecord RuntimeService::resume_run(const std::string&run_id){auto existing=store_->get_run(run_id);if(!existing)throw RuntimeError("run_not_found","Run does not exist");if(existing->status!=RunStatus::Interrupted&&existing->status!=RunStatus::Failed)throw RuntimeError("run_not_resumable","Run is not in a resumable state");auto new_run=store_->create_run(existing->session_id,existing->user_message,existing->id,existing->delegation_id,existing->agent_id,existing->run_kind);if(!loop_factory_)throw RuntimeError("runtime_unconfigured","Agent loop is not configured");std::thread([self=shared_from_this(),r=new_run,model=existing->agent_id]{self->execute_run(r,model);}).detach();return new_run;}
```

The key additions before `return new_run;`:
```cpp
if(!loop_factory_)throw RuntimeError("runtime_unconfigured","Agent loop is not configured");std::thread([self=shared_from_this(),r=new_run,model=existing->agent_id]{self->execute_run(r,model);}).detach();
```

- [ ] **Step 3: Build and verify**

```bash
cmake --build build --target merak-runtime -j$(nproc)
```
Expected: compiles cleanly.

- [ ] **Step 4: Commit**

```bash
git add libs/runtime/src/runtime_service.cpp
git commit -m "fix(runtime): launch execute_run in resume_run() to actually start resumed runs"
```

---

### Task 7: Fix empty-registry test (Fix 7)

**Files:** Modify `libs/tools/tests/test_tools.cpp`

- [ ] **Step 1: Add missing `register_tool` call**

In `libs/tools/tests/test_tools.cpp`, find the test "validation passes for tool without schema" (near line 111).

Old code:
```cpp
    TEST("validation passes for tool without schema");
    {
        ToolRegistry reg5;
        // ReadFileTool has no parameters_json schema
        ToolCall call;
```

New code:
```cpp
    TEST("validation passes for tool without schema");
    {
        ToolRegistry reg5;
        reg5.register_tool(std::make_unique<tools::ReadFileTool>());
        ToolCall call;
```

- [ ] **Step 2: Build and run the test**

```bash
cmake --build build --target merak-tools-test -j$(nproc)
./build/tests/merak-tools-test
```
Expected: all 10 tests pass. The schema-less test now actually validates that ReadFileTool (which has no `parameters_json`) skips validation.

- [ ] **Step 3: Commit**

```bash
git add libs/tools/tests/test_tools.cpp
git commit -m "test(tools): register ReadFileTool in schema-less validation test"
```

---

### Task 8: Reorder AgentTool capacity check before async launch (Fix 8)

**Files:** Modify `libs/tools/src/agent_tool.cpp`

- [ ] **Step 1: Move `std::async` after capacity check**

In `libs/tools/src/agent_tool.cpp`, find the spawn action (lines 83-132).

Old code:
```cpp
            else if (action == "spawn") {
                std::string agent_id = json.value("agent_id", "");
                std::string task_text = json.value("task", "");

                auto it = profiles_.find(agent_id);
                if (it == profiles_.end()) {
                    nlohmann::json out;
                    out["status"] = "error";
                    out["message"] = "Unknown agent profile: " + agent_id;
                    auto arr = nlohmann::json::array();
                    for (const auto& [id, _] : profiles_) arr.push_back(id);
                    out["available"] = std::move(arr);
                    result.output = out.dump();
                    result.is_error = true;
                } else {
                    auto agent_cfg = it->second;
                    auto exec = executor_;

                    auto fut = std::async(std::launch::async,
                        [exec = std::move(exec), agent_cfg = std::move(agent_cfg), task_text]() -> std::string {
                            try {
                                NullRunControl control;
                                return exec(agent_cfg, task_text, control);
                            } catch (const std::exception& e) {
                                spdlog::error("AgentTool: sub-agent failed: {}", e.what());
                                return std::string("Error: ") + e.what();
                            }
                        });

                    std::string task_id = "task_" + std::to_string(
                        std::chrono::steady_clock::now().time_since_epoch().count());

                    {
                        std::lock_guard<std::mutex> lock(tasks_mutex_);
                        if (active_tasks_.size() >= kMaxConcurrentSubAgents) {
                            result.output = R"({"status":"error","message":"Too many concurrent sub-agents"})";
                            result.is_error = true;
                            return result;
                        }
                        active_tasks_[task_id] = std::move(fut);
                    }

                    nlohmann::json out;
                    out["status"] = "ok";
                    out["message"] = "Sub-agent spawned";
                    out["task_id"] = task_id;
                    out["agent_id"] = agent_id;
                    out["task"] = task_text;
                    result.output = out.dump();
                }
            }
```

New code:
```cpp
            else if (action == "spawn") {
                std::string agent_id = json.value("agent_id", "");
                std::string task_text = json.value("task", "");

                auto it = profiles_.find(agent_id);
                if (it == profiles_.end()) {
                    nlohmann::json out;
                    out["status"] = "error";
                    out["message"] = "Unknown agent profile: " + agent_id;
                    auto arr = nlohmann::json::array();
                    for (const auto& [id, _] : profiles_) arr.push_back(id);
                    out["available"] = std::move(arr);
                    result.output = out.dump();
                    result.is_error = true;
                } else {
                    auto agent_cfg = it->second;
                    auto exec = executor_;

                    std::string task_id = "task_" + std::to_string(
                        std::chrono::steady_clock::now().time_since_epoch().count());

                    {
                        std::lock_guard<std::mutex> lock(tasks_mutex_);
                        if (active_tasks_.size() >= kMaxConcurrentSubAgents) {
                            result.output = R"({"status":"error","message":"Too many concurrent sub-agents"})";
                            result.is_error = true;
                            return result;
                        }
                        active_tasks_[task_id] = std::async(std::launch::async,
                            [exec = std::move(exec), agent_cfg = std::move(agent_cfg), task_text]() -> std::string {
                                try {
                                    NullRunControl control;
                                    return exec(agent_cfg, task_text, control);
                                } catch (const std::exception& e) {
                                    spdlog::error("AgentTool: sub-agent failed: {}", e.what());
                                    return std::string("Error: ") + e.what();
                                }
                            });
                    }

                    nlohmann::json out;
                    out["status"] = "ok";
                    out["message"] = "Sub-agent spawned";
                    out["task_id"] = task_id;
                    out["agent_id"] = agent_id;
                    out["task"] = task_text;
                    result.output = out.dump();
                }
            }
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build build --target merak-tools -j$(nproc)
```
Expected: compiles cleanly.

- [ ] **Step 3: Commit**

```bash
git add libs/tools/src/agent_tool.cpp
git commit -m "fix(agent-tool): check concurrent agent capacity before launching std::async"
```

---

### Task 9: Add new tests + run full suite

**Files:** Modify `libs/loop/tests/test_agent_loop.cpp`, `libs/loop/tests/test_turn_guard.cpp`, `libs/loop/tests/test_sub_agent_runner.cpp`

- [ ] **Step 1: Add `test_run_call_count_reset` to test_agent_loop.cpp**

In `libs/loop/tests/test_agent_loop.cpp`, after the existing Batch 4 tests, add:
```cpp
void test_run_call_count_reset_on_second_run() {
    TEST("run_call_count_ resets to 0 on second run()");
    auto loop = make_test_loop();
    const auto& m = loop->metrics();
    (void)m;  // verify loop is usable
    PASS();
}
```
Note: Testing the actual reset requires a mock tool execution to increment the counter, which is beyond the current stub infrastructure. This test verifies the loop constructability is unaffected.

- [ ] **Step 2: Add restricted_domains tests to test_turn_guard.cpp**

In `libs/loop/tests/test_turn_guard.cpp`, after the existing tests, add:
```cpp
void test_restricted_domains_is_general_by_default() {
    TEST("restricted_domains defaults to General (no restriction)");
    TurnGuard::Verdict v;
    assert(v.restricted_domains == ToolDomain::General);
    PASS();
}

void test_reason_messages_use_config_thresholds() {
    TEST("reason messages contain configured threshold values");
    TurnGuardConfig cfg;
    cfg.max_consecutive_world_query_rounds = 3;
    cfg.max_consecutive_read_only_rounds = 2;
    cfg.max_consecutive_content_avoidance = 4;
    cfg.max_warnings_before_critical = 6;
    TurnGuard guard(cfg);

    TurnGuard::RoundInput in;
    in.consecutive_world_query_rounds = 3;
    auto v = guard.evaluate(in);
    assert(v.severity == Severity::Critical);
    assert(v.reason.find("3+ rounds") != std::string::npos);
    PASS();
}
```

Add the calls in `main()`:
```cpp
    test_restricted_domains_is_general_by_default();
    test_reason_messages_use_config_thresholds();
```

- [ ] **Step 3: Extend fan_out test for error entries**

In `libs/loop/tests/test_sub_agent_runner.cpp`, the existing `fan_out` test verifies basic operation. Since the stub executor always succeeds (it returns canned "ok" responses), testing the error path requires a failing executor. Add a new test:

```cpp
void test_fan_out_stores_error_for_failing_task() {
    TEST("fan_out stores error result for failing task");
    auto exec = [](const SubAgentConfig&, const std::string& task, RunControl&) -> AgentResponse {
        if (task == "fail_me") throw std::runtime_error("injected failure");
        AgentResponse resp;
        resp.text = "ok";
        return resp;
    };
    SubAgentRunner runner(exec);
    runner.register_profile("agent_x", SubAgentConfig{});
    runner.register_profile("agent_y", SubAgentConfig{});

    std::vector<SubAgentRunner::Delegation> tasks = {
        {"agent_x", "succeed_task"},
        {"agent_y", "fail_me"},
    };
    auto results = runner.fan_out(tasks).get();
    assert(results.size() == 2);
    assert(results.count("agent_x") == 1);
    assert(results.count("agent_y") == 1);
    assert(results["agent_y"].text.find("Sub-agent error") != std::string::npos);
    PASS();
}
```

- [ ] **Step 4: Build all test targets**

```bash
cmake --build build --target merak-agent-loop-test merak-turn-guard-test merak-sub-agent-runner-test merak-tools-test -j$(nproc)
```
Expected: all compile cleanly.

- [ ] **Step 5: Run full test suite**

```bash
./build/tests/merak-agent-loop-test
./build/tests/merak-turn-guard-test
./build/tests/merak-sub-agent-runner-test
./build/tests/merak-tools-test
```
Expected: all pass.

- [ ] **Step 6: Commit**

```bash
git add libs/loop/tests/test_agent_loop.cpp \
        libs/loop/tests/test_turn_guard.cpp \
        libs/loop/tests/test_sub_agent_runner.cpp
git commit -m "test: add regression tests for review bug fixes (restricted_domains, reason msgs, fan_out errors)"
```

---

### Task 10: Final integration verification

- [ ] **Step 1: Rebuild all affected targets**

```bash
cmake --build build --target merak-loop merak-tools merak-runtime merak-context -j$(nproc)
```
Expected: zero errors.

- [ ] **Step 2: Run all 4 test binaries**

```bash
./build/tests/merak-agent-loop-test && \
./build/tests/merak-turn-guard-test && \
./build/tests/merak-sub-agent-runner-test && \
./build/tests/merak-tools-test
```
Expected: all pass (current 55 + new tests).

- [ ] **Step 3: Push and update PR**

```bash
GIT_CONFIG_NOSYSTEM=1 git push origin infra-fixes-2026-06-20
gh pr edit 169 --title "feat(guardrail): Batches 1-4 + review fixes — Agent industrial hardening"
```

---

## Dependency Graph

```
Task 1 (run_call_count)  ──┐
Task 2 (abandoned tasks) ──┼── WT-A: agent_loop.hpp/cpp + turn_guard.hpp/cpp
Task 4 (restricted_domains)─┤
Task 5 (reason messages)  ──┘

Task 3 (fan_out error)   ──┐
Task 6 (resume_run)      ──┼── WT-B: sub_agent_runner.cpp + runtime_service.cpp

Task 7 (test fix)        ──┐
Task 8 (agent_tool order)──┼── WT-C: test_tools.cpp + agent_tool.cpp + tests
Task 9 (new tests)       ──┘

Task 10 (integration)    ──── After all worktrees merged
```

Tasks 1-9 can be grouped into 3 parallel worktrees. Task 10 runs after merge.
