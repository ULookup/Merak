# AgentLoop Session-Level Lifecycle & Context Safety

## Problem

1. **Cross-run context loss**: Each `start_run` creates a new `AgentLoop` with empty `session_history_`. Previous conversation—including all tool roundtrips—is lost. When user sends a follow-up, the LLM has no memory of prior turns.

2. **Fixed-tool-count hang**: After a consistent number of tool calls, the agent silently hangs. Two contributing causes:
   - `fit_in_budget` truncates history from the front and can split `assistant(tool_calls)` / `tool(tool_call_id)` pairs, producing orphaned `tool` messages that break API requests.
   - curl has no total timeout (`CURLOPT_TIMEOUT`), only low-speed detection. If the LLM server sends data slowly but never completes the stream, curl blocks indefinitely.

## Design

### Core Decision: AgentLoop lives at Session scope, not Run scope

**Before:**
```
Session → Run1 → new AgentLoop → run() → destroy
Session → Run2 → new AgentLoop → run() → destroy  ← history lost
```

**After:**
```
Session → AgentLoop created on first run
       → Run1: loop->run(msg1) → history naturally retained
       → Run2: loop->run(msg2) → history accumulates
       → Run3: loop->run(msg3)
       → Session ends → AgentLoop destroyed
```

AgentLoop is no longer a one-shot object. It persists for the session lifetime. `session_history_` accumulates across runs without manual restoration.

### Changes

#### 1. AgentLoop API refactor

**`libs/loop/include/merak/agent_loop.hpp`**

- Remove `initial_history` and `append_user_message` parameters from `run()`.
- Add `restore_history(vector<Message>)` — called once when AgentLoop is created (on first run or after server restart) to load history from journal.
- `run(user_message, control)` now always appends the user message to the existing `session_history_`, then enters the ReAct loop.
- Extract `run_loop(control)` as the private ReAct loop body (same logic, just lifted out of `run()`).
- Add public `session_history()` getter.

**`libs/loop/src/agent_loop.cpp`**

- `run()`: push user message onto `session_history_`, clear `tool_failure_streak_`, call `run_loop()`.
- `run_loop()`: the existing while/turn loop (LLM call → tool execution → repeat), unchanged logic.
- `build_context()`: remove `user_message` parameter (no longer needed for memory search key—use the current user message from history).

#### 2. RuntimeService caches AgentLoop per session

**`libs/runtime/include/merak/runtime_service.hpp`**

New members:
```cpp
std::map<std::string, std::shared_ptr<AgentLoop>> session_loops_;
std::mutex session_loops_mutex_;
```

**`libs/runtime/src/runtime_service.cpp` — `execute_run`**

Logic:
1. Look up `session_loops_` by `session_id`.
2. If not found: create via `loop_factory_()`, call `restore_messages()` from journal, call `loop->restore_history()`.
3. If found: reuse the existing AgentLoop (its `session_history_` already has all prior context).
4. Call `loop->run(user_message, control)`.
5. AgentLoop stays in `session_loops_` after run completes (no cleanup in the normal path).

#### 3. fit_in_budget message pairing protection

**`libs/context/src/token_counter.cpp` — `fit_in_budget`**

After counting from the end, if the first kept message has `role == "tool"`, walk backwards to include the preceding `assistant` (with `tool_calls`) or `user` message. This prevents orphaned tool messages from being sent to the LLM API.

#### 4. Tool result compaction (microcompact)

**New files: `libs/context/include/merak/tool_result_compactor.hpp`, `libs/context/src/tool_result_compactor.cpp`**

Reference: Astra's `microcompact.rs`. Never deletes messages, only replaces `content` of old tool results with placeholders.

- Compacts only readable tools (read_file, grep, glob, list_dir, etc.). Never compacts: bash, write_file, str_replace, multi_edit, delete_file.
- Keeps the most recent N results intact (default N=6).
- Only triggers when context pressure exceeds threshold (default 60%).
- Placeholder format: `[已压缩] 工具结果过长，原始长度 N 字符。如需重新获取请重新调用工具。`

#### 5. curl total timeout

**`libs/llm/src/anthropic_provider.cpp`, `libs/llm/src/openai_provider.cpp`**

Add `curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L)` (5-minute total timeout). This is a safety net; the 30s low-speed timeout still provides faster detection for stalled transfers.

### Unchanged

- ReAct pattern (`Thinking → Acting → Observing` loop)
- Journal persistence (JSONL via `SessionStore`)
- HTTP API surface
- Concurrency model (`std::async` + synchronous I/O)
- `has_unfinished_run` guard (one run per session at a time)

### Files Changed

| File | Change | Type |
|------|--------|------|
| `libs/loop/include/merak/agent_loop.hpp` | API refactor: `restore_history()`, simplified `run()`, `run_loop()`, getter | Core |
| `libs/loop/src/agent_loop.cpp` | `run()` appends to history; `run_loop()` extracted | Core |
| `libs/runtime/include/merak/runtime_service.hpp` | `session_loops_` cache | Core |
| `libs/runtime/src/runtime_service.cpp` | `execute_run` reuses AgentLoop per session | Core |
| `libs/context/src/token_counter.cpp` | `fit_in_budget` pairing guard | Bug fix |
| `libs/context/include/merak/tool_result_compactor.hpp` | New: tool result compactor header | New |
| `libs/context/src/tool_result_compactor.cpp` | New: tool result compactor impl | New |
| `libs/llm/src/anthropic_provider.cpp` | `CURLOPT_TIMEOUT` | Bug fix |
| `libs/llm/src/openai_provider.cpp` | `CURLOPT_TIMEOUT` | Bug fix |

### End-to-End Flow

```
Session "abc123" created (no AgentLoop yet)

User sends "分析项目"
  → execute_run:
      session_loops_ miss → loop_factory_() creates AgentLoop
      → restore_messages("abc123") → [] (no prior history)
      → loop->restore_history([])
      → loop->run("分析项目")
          session_history_ = [user:"分析项目"]
          Round 0: LLM → tool_calls [list_dir, read_file]
          session_history_ += [assistant+tc, tool:list_dir, tool:read_file]
          Round 1: LLM → tool_calls [grep]
          session_history_ += [assistant+tc, tool:grep]
          Round 2: LLM → text response (no tool_calls → return)
          session_history_ += [assistant:"分析结果..."]
      → AgentLoop stays in session_loops_, history intact

User sends "继续看测试"
  → execute_run:
      session_loops_ hit → reuse existing AgentLoop
      → loop->run("继续看测试")
          session_history_ += [user:"继续看测试"]  ← appended to full history
          Round 0: LLM sees complete prior context → ...
      → AgentLoop stays in session_loops_
```
