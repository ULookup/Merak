# Design: PR #169 Review Bug Fixes

**Date:** 2026-06-21  
**Parent PR:** #169 (Agent Industrial Hardening, Batches 1-4)  
**Source:** Code review and functional verification findings  
**Branch:** `infra-fixes-2026-06-20`  

---

## 1. Background

Two review agents independently audited PR #169 (4 batches, 59 files, +2554 lines).  
Findings were verified by 3 source-code analysis agents against actual implementation.  
Result: **8 confirmed bugs** (1 false positive), ranging from data races to dead features.

This document designs production-grade fixes for all 8 issues, following patterns from  
Claude Code, Codex, and OpenAI's agent frameworks where applicable.

---

## 2. Fix Designs

### Fix 1 — `run_call_count_` never reset across runs

**Problem:** `run_call_count_` is only incremented in `handle_tool_calls()`, never reset.  
Consecutive `run()` / `resume()` calls on the same AgentLoop instance accumulate counts,  
causing the per-run rate limit (`max_calls_per_run`, default 500) to trigger prematurely.

**Design:**
- Reset `run_call_count_ = 0` in `run()` (alongside `tool_failure_streak_.clear()`)  
- Reset `run_call_count_ = 0` in `resume()` (alongside existing resets)  
- Reset `run_call_count_ = 0` in `restore_history()` (a restored session is a fresh run)

**Files:** `libs/loop/src/agent_loop.cpp` (+3 lines)

---

### Fix 2 — Tool timeout future destructor blocks main loop

**Problem:** All tool `execute()` methods return `std::async(std::launch::async, ...)`.  
When `wait_for` returns `timeout`, the `continue` statement destroys `result_future` —  
and per C++11 [futures.async]/5, the destructor **blocks** until the async task completes.  
The timeout is "soft": it reports an error but still waits.

**Design (Claude Code abandoned-task pattern):**

1. New private member in `AgentLoop`:
   ```cpp
   std::vector<std::future<ToolResult>> abandoned_tasks_;
   static constexpr size_t kMaxAbandonedTasks = 32;
   ```

2. New private method `drain_abandoned_tasks()`:
   - Non-blocking poll: `f.wait_for(0ms) == ready`  
   - Remove completed futures from the vector via erase-remove idiom  
   - Called at the start of each turn in `run_loop()`

3. Timeout branch in `handle_tool_calls()`:
   - Call `ctx.cancellation->cancel()` to signal the tool thread  
   - `std::move(result_future)` into `abandoned_tasks_` (avoids blocking destructor)  
   - If `abandoned_tasks_.size() >= kMaxAbandonedTasks`, log error and `.get()` the oldest  
     (safety valve against unbounded accumulation)

4. Add `int abandoned_tasks = 0` to `RunMetrics` for observability, incremented on each timeout.

**Rationale:** Claude Code uses this same pattern — abandoned futures are collected and  
polled non-blockingly. The cancellation token is signaled on timeout as a cooperative hint;  
tools that check it can stop early. Tools that don't check it run to completion in the  
background and get cleaned up in a subsequent `drain_abandoned_tasks()` call.

**Files:** `libs/loop/include/merak/agent_loop.hpp` (+4), `libs/loop/src/agent_loop.cpp` (+25)

---

### Fix 3 — `fan_out` error result constructed but never stored

**Problem:** In `SubAgentRunner::fan_out()`, the catch block constructs `AgentResponse err`  
and logs a warning, but never inserts the error into the `results` map. The caller receives  
N-1 results for N tasks with no indication of which task failed.

**Design (restructure to capture agent_id in catch):**

Current code iterates `batch` futures indexed by position. The agent_id is captured in the  
lambda that produced each future, but lost in the batch collection loop. Fix:

```cpp
for (size_t i = 0; i < batch.size(); i++) {
    // tasks[batch_start + i] holds the original Delegation with agent_id
    auto& d = tasks[batch_start + i];
    try {
        auto result = batch[i].get();
        results[result.first] = result.second;
    } catch (const std::exception& e) {
        AgentResponse err;
        err.text = std::string("Sub-agent error: ") + e.what();
        results[d.agent_id] = err;   // store with agent_id as key
        spdlog::warn("SubAgentRunner: fan_out task '{}' failed: {}", d.agent_id, e.what());
    }
}
```

This requires tracking `batch_start` alongside the `batch` vector throughout the while loop,  
so the index into `tasks` can be reconstructed for the catch block.

**Files:** `libs/loop/src/sub_agent_runner.cpp` (~8 lines changed)

---

### Fix 4 — `restricted_tools` hardcoded string list should use `ToolDomain`

**Problem:** TurnGuard detects world-query-only loops correctly via `ToolDomain::WorldQuery`  
bitflags (in `agent_loop.cpp`), but the remediation — restricting tools — uses a hardcoded  
list of 5 tool names in `turn_guard.cpp`. New WorldQuery tools are not blocked.

**Design (semantic domain-based restriction):**

**Step A — Replace `restricted_tools` strings with `restricted_domains` bitmask:**

In `TurnGuard::Verdict`:
```cpp
// OLD:
std::vector<std::string> restricted_tools;
// NEW:
ToolDomain restricted_domains = ToolDomain::General;  // General=0 means "none"
```

**Step B — TurnGuard sets domain mask instead of name list:**

`turn_guard.cpp` line 23:
```cpp
// OLD:
v.restricted_tools = {"query_map", "query_world", ...};
// NEW:
v.restricted_domains = ToolDomain::WorldQuery;
```

**Step C — Consumption side in agent_loop.cpp uses domain check:**

Current code (lines 124-133):
```cpp
if (!restricted_tools_.empty()) {
    std::unordered_set<std::string> blocked(restricted_tools_.begin(), restricted_tools_.end());
    // ... filter by name match ...
}
```

New code:
```cpp
if (restricted_domains_ != ToolDomain::General) {
    std::vector<ToolSpec> filtered;
    for (auto& ts : tool_specs) {
        if (!(tools_->domain_of(ts.name) & restricted_domains_)) {
            filtered.push_back(ts);
        }
    }
    req.tools = std::move(filtered);
}
```

**Step D — AgentLoop member type change:**

```cpp
// OLD:
std::vector<std::string> restricted_tools_;
// NEW:
ToolDomain restricted_domains_ = ToolDomain::General;
```

**Extensibility:** Future verdicts can set `v.restricted_domains = ToolDomain::Write`  
to block write tools during planning mode, or combine: `ToolDomain::Write | ToolDomain::WorldQuery`.

**Files:** `libs/loop/include/merak/turn_guard.hpp`, `libs/loop/src/turn_guard.cpp`,  
`libs/loop/include/merak/agent_loop.hpp`, `libs/loop/src/agent_loop.cpp` (~15 lines changed)

---

### Fix 5 — TurnGuard reason messages contain hardcoded threshold numbers

**Problem:** 4 reason strings embed literal integers matching default config values.  
When config thresholds change, the messages still report the old numbers.

Current hardcoded strings:
| Line | String | Config field |
|------|--------|-------------|
| 22 | `"5+ rounds of world-only queries..."` | `max_consecutive_world_query_rounds` |
| 30 | `"3+ rounds without write operations"` | `max_consecutive_read_only_rounds` |
| 36 | `"3x refusal to advance narrative"` | `max_consecutive_content_avoidance` |
| 68 | `"4+ warnings in this run"` | `max_warnings_before_critical` |

**Design:** Dynamic formatting with `std::to_string`:
```cpp
v.reason = std::to_string(config_.max_consecutive_world_query_rounds) +
    "+ rounds of world-only queries without narrative output";
v.reason = std::to_string(config_.max_consecutive_read_only_rounds) +
    "+ rounds without write operations";
v.reason = std::to_string(config_.max_consecutive_content_avoidance) +
    "x refusal to advance narrative";
// Line 68 (after warning_count_ check):
v.reason = std::to_string(config_.max_warnings_before_critical) +
    "+ warnings in this run";
```

Also fix the stall force_stop message (line 16) to use `StallDetector::Config`:
```cpp
v.reason = "force_stop: " + std::to_string(stall_detector_config_.consecutive_identical) +
    " consecutive identical tool-call rounds";
```
But StallDetector::Config may not be accessible from TurnGuard. For now, keep line 16  
as-is (force_stop = 5 is a separate config in StallDetector, not TurnGuardConfig).

**Files:** `libs/loop/src/turn_guard.cpp` (4 lines changed)

---

### Fix 6 — `resume_run()` creates DB record but never starts execution

**Problem:** `RuntimeService::resume_run()` validates state and calls `store_->create_run()`,  
but never calls `execute_run()`. The HTTP handler returns 202 for a run that never produces output.  
Compare with `start_run()` which launches `execute_run` in a detached thread.

**Design:** Match the `start_run()` pattern exactly:

```cpp
RunRecord RuntimeService::resume_run(const std::string& run_id) {
    auto existing = store_->get_run(run_id);
    if (!existing) throw RuntimeError("run_not_found", "Run does not exist");
    if (existing->status != RunStatus::Interrupted &&
        existing->status != RunStatus::Failed)
        throw RuntimeError("run_not_resumable", "Run is not in a resumable state");

    auto new_run = store_->create_run(
        existing->session_id, existing->user_message,
        existing->id, existing->delegation_id,
        existing->agent_id, existing->run_kind);

    if (!loop_factory_)
        throw RuntimeError("runtime_unconfigured", "Agent loop is not configured");

    // Launch execution in background thread — same pattern as start_run()
    std::thread([self = shared_from_this(), r = new_run, model = existing->agent_id] {
        self->execute_run(r, model);
    }).detach();

    return new_run;
}
```

Additionally, ensure cancellation token is registered for the new run in `execute_run()`  
(the existing `execute_run` should already handle this; verify during implementation).

**Files:** `libs/runtime/src/runtime_service.cpp` (+5 lines)

---

### Fix 7 — `test_tools.cpp` empty-registry test passes for wrong reason

**Problem:** The test "validation passes for tool without schema" creates an empty `reg5`  
without registering `ReadFileTool`. The assertion passes because the error is "Tool not found",  
not because validation was skipped for a schema-less tool.

**Design:** Register the tool properly:
```cpp
TEST("validation passes for tool without schema");
{
    ToolRegistry reg5;
    reg5.register_tool(std::make_unique<tools::ReadFileTool>());  // ADD THIS LINE
    ToolCall call;
    call.name = "read_file";
    call.id = "call_4";
    call.arguments = R"({"path": "/tmp/test"})";
    auto result = reg5.execute(call, {}).get();
    assert(result.output.find("Invalid arguments") == std::string::npos);
}
PASS();
```

**Files:** `libs/tools/tests/test_tools.cpp` (+1 line)

---

### Fix 8 — `AgentTool::execute()` launches async before capacity check

**Problem:** In the `"spawn"` action, `std::async(std::launch::async, ...)` is launched  
at line 101, but the capacity check (`active_tasks_.size() >= kMaxConcurrentSubAgents`)  
happens at line 117 inside the mutex lock. When capacity is exceeded, the function returns  
an error — but `fut`'s destructor blocks until the already-launched sub-agent completes.  

**Design:** Move `std::async` launch after the capacity check, inside the mutex scope:

```cpp
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
        // Launch only after capacity is confirmed
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
```

Note: `task_id` generation moves before the lock (it only reads `steady_clock`, no shared state).  
The `std::async` lambdas capture `exec`, `agent_cfg`, `task_text` by value (move), so they  
are independent of the lock scope.

**Files:** `libs/tools/src/agent_tool.cpp` (~15 lines reordered)

---

## 3. Impact Summary

| Fix | Files | Lines | Risk |
|-----|-------|-------|------|
| 1  | `agent_loop.cpp` | +3 | Low — trivial reset |
| 2  | `agent_loop.hpp`, `agent_loop.cpp` | +29 | Medium — new member + async cleanup pattern |
| 3  | `sub_agent_runner.cpp` | ~8 | Low — restructure catch block |
| 4  | `turn_guard.hpp/cpp`, `agent_loop.hpp/cpp` | ~15 | Medium — type change ripples across files |
| 5  | `turn_guard.cpp` | 4 lines | Low — string formatting |
| 6  | `runtime_service.cpp` | +5 | Medium — must verify execute_run handles resumed runs correctly |
| 7  | `test_tools.cpp` | +1 | Low — add missing registration |
| 8  | `agent_tool.cpp` | ~15 reorder | Low — pure reorder, no logic change |

**Total:** ~80 lines across 8 files, zero new APIs, backward compatible.

## 4. Non-Goals

- Fixing the `GET /v1/runs/:id/output` hardcoded `turn_count: 0` (Batch 3 partial impl — tracked separately)  
- Fixing `SubAgentConfig::model` silently ignored (Batch 2 partial impl — tracked separately)  
- Fixing `HttpLimits` missing `max_field_length` (Batch 3 partial impl — tracked separately)  
- These are feature gaps, not bugs. The review found them but they don't break existing functionality.

## 5. Test Plan

- `test_agent_loop.cpp`: Add `test_run_call_count_reset()` — verify count is 0 after run()  
- `test_agent_loop.cpp`: Add `test_abandoned_tasks_drain()` — verify drain doesn't crash  
- `test_sub_agent_runner.cpp`: Extend fan_out test to verify error entries in results map  
- `test_turn_guard.cpp`: Add test verifying `restricted_domains` is set correctly  
- `test_turn_guard.cpp`: Add test verifying reason messages contain config threshold numbers  
- `test_tools.cpp`: Fix empty-registry test (Fix 7)  
- `test_agent_loop.cpp`: Add `test_tool_rate_limit_reset()` — verify per-run limit resets  
- All 55 existing tests must continue to pass

## 6. Rollout

All fixes are on `infra-fixes-2026-06-20` branch. After implementation:  
1. Rebuild all targets  
2. Run full test suite  
3. Push and update PR #169
