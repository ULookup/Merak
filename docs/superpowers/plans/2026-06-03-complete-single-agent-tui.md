# Complete Single-Agent TUI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete Merak's single-agent inline TUI with streamed reasoning, turn summaries, session model overrides, a practical context panel, unified views, and workspace-scoped approval rules with a complex approval queue.

**Architecture:** Keep `merak serve` as the stateful runtime and `merak tui` as an HTTP + SSE client. Add typed data models at the core boundary, keep permission classification in `libs/tools`, persist session overrides in `libs/storage`, and translate wire events into TUI mutations in a dedicated bridge so `main.cpp` becomes composition code.

**Tech Stack:** C++23, CMake, SQLite, nlohmann/json, cpp-httplib, libcurl, FTXUI color primitives, inline ANSI terminal rendering, CTest.

---

## File Structure

Create:

- `libs/tools/include/merak/permission_policy.hpp`: approval metadata, risk classification, safe persistent rule generation, and workspace JSON rule storage.
- `libs/tools/src/permission_policy.cpp`: policy implementation.
- `cli/src/tui/runtime_event_bridge.hpp`: typed SSE-to-TUI translation and replay behavior.
- `cli/src/tui/view_stack.hpp`: unified overlay navigation state.
- `cli/src/tui/approval/approval_queue.hpp`: TUI approval entries, deduplication, focus, and action navigation.
- `cli/tests/test_tui_components.cpp`: deterministic TUI component tests.

Modify:

- `libs/core/include/merak/llm_provider.hpp`: distinguish answer and reasoning stream chunks.
- `libs/core/include/merak/execution.hpp`: add reasoning emission and structured approval decisions.
- `libs/config/include/merak/config.hpp`: use configured model catalog limits in runtime metadata.
- `libs/llm/src/anthropic_provider.cpp`: emit reasoning chunks while preserving replay blocks.
- `libs/llm/src/openai_provider.cpp`: emit OpenAI-compatible reasoning fields when present.
- `libs/loop/include/merak/agent_loop.hpp`: support per-run model selection.
- `libs/loop/src/agent_loop.cpp`: forward reasoning and apply model overrides.
- `libs/storage/include/merak/session_store.hpp`: persist session model override and richer approval metadata.
- `libs/storage/src/session_store.cpp`: SQLite migration and persistence methods.
- `libs/runtime/include/merak/runtime_service.hpp`: model catalog, override API, permission policy wiring, and approval actions.
- `libs/runtime/src/runtime_service.cpp`: reasoning events, override selection, rule matching, and approval resolution.
- `libs/http/include/merak/http_server.hpp`: model and override handlers.
- `libs/http/src/http_server.cpp`: HTTP routes and richer approval body.
- `cli/src/client/runtime_client.hpp`: model and approval action client calls.
- `cli/src/client/runtime_client.cpp`: HTTP implementations.
- `cli/src/tui/history_cell/history_cell.hpp`: reasoning cell and corrected summary rendering.
- `cli/src/tui/chat_timeline.hpp`: reasoning transitions and turn summary entry point.
- `cli/src/tui/components/status_bar.hpp`: context limit and pending approval chips.
- `cli/src/tui/composer/chat_composer.hpp`: previous-user restore support.
- `cli/src/tui/terminal_event_reader.hpp`: `Ctrl+R`.
- `cli/src/tui/screen_manager.hpp`: view stack, context/model views, approval queue, and retry shortcut.
- `cli/src/main.cpp`: composition and bridge wiring.
- `tests/CMakeLists.txt`: register new tests.
- Existing storage, runtime, HTTP, tools, and client tests: extend contract coverage.

## Task 1: Streamed Reasoning Contract

**Files:**
- Modify: `libs/core/include/merak/llm_provider.hpp`
- Modify: `libs/core/include/merak/execution.hpp`
- Modify: `libs/llm/src/anthropic_provider.cpp`
- Modify: `libs/llm/src/openai_provider.cpp`
- Modify: `libs/loop/src/agent_loop.cpp`
- Modify: `libs/runtime/src/runtime_service.cpp`
- Test: `libs/runtime/tests/test_runtime.cpp`

- [ ] **Step 1: Write a failing runtime reasoning test**

Add a small test-only control entry point by constructing a loop with a fake provider that emits:

```cpp
on_chunk(StreamChunk{StreamChunk::Kind::Reasoning, "inspect state", false});
on_chunk(StreamChunk{StreamChunk::Kind::Answer, "done", false});
```

Assert the runtime journal contains `reasoning_delta`, `reasoning_completed`,
then `text_delta` in order.

- [ ] **Step 2: Run the focused test and verify failure**

Run:

```bash
cmake --build build --target merak-runtime-test -j
ctest --test-dir build -R merak-runtime-test --output-on-failure
```

Expected: compile failure because `StreamChunk::Kind` and reasoning control
methods do not exist.

- [ ] **Step 3: Add the minimal stream contract**

Use:

```cpp
struct StreamChunk {
    enum class Kind { Answer, Reasoning, ToolCall };
    Kind kind = Kind::Answer;
    std::string text;
    bool is_final = false;
};
```

Add `emit_reasoning_delta(std::string)` and `emit_reasoning_completed()` to
`RunControl`. In `AgentLoop`, forward `Kind::Reasoning` chunks without adding
them to `AgentResponse::text`. Before the first answer chunk and at provider
completion, emit `reasoning_completed()` once if reasoning was seen.

- [ ] **Step 4: Emit provider reasoning chunks**

For Anthropic `thinking_delta`, continue preserving the content block and also
call:

```cpp
on_chunk(StreamChunk{StreamChunk::Kind::Reasoning,
                     delta.value("thinking", ""), false});
```

For OpenAI-compatible responses, accept either `reasoning_content` or
`reasoning` on the streamed delta and emit `Kind::Reasoning`.

- [ ] **Step 5: Run tests**

Run:

```bash
cmake --build build --target merak-runtime-test merak-http-test -j
ctest --test-dir build -R "merak-runtime-test|merak-http-test" --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add libs/core libs/llm libs/loop libs/runtime
git commit -m "feat: stream reasoning through runtime events"
```

## Task 2: TUI Reasoning Cell And Turn Summary

**Files:**
- Modify: `cli/src/tui/history_cell/history_cell.hpp`
- Modify: `cli/src/tui/chat_timeline.hpp`
- Modify: `cli/src/tui/screen_manager.hpp`
- Create: `cli/tests/test_tui_components.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing cell tests**

Add assertions for:

```cpp
ChatTimeline timeline;
timeline.append_reasoning("inspect ");
timeline.append_reasoning("state");
assert(timeline.active()->is_live());
timeline.append_assistant("done");
assert(timeline.committed().front()->to_json()["type"] == "reasoning");
timeline.add_summary(1250, 100, 20, 2, 120, true);
assert(timeline.committed().back()->to_json()["type"] == "turn_summary");
```

Also assert rendered summary text contains `1.2s`, input, output, tool count,
and cumulative token count.

- [ ] **Step 2: Run the new test and verify failure**

Run:

```bash
cmake --build build --target merak-tui-components-test -j
```

Expected: failure because the target and reasoning methods do not exist.

- [ ] **Step 3: Add `ReasoningCell` and summary formatting**

Implement a dim reasoning cell with JSON:

```cpp
{{"type", "reasoning"}, {"text", reasoning_}}
```

Add `append_reasoning`, `finish_reasoning`, and summary creation to
`ChatTimeline`. Ensure `append_assistant` commits reasoning before creating an
assistant cell.

- [ ] **Step 4: Register and run TUI tests**

Add `merak-tui-components-test` to `tests/CMakeLists.txt`, then run:

```bash
cmake --build build --target merak-tui-components-test -j
ctest --test-dir build -R merak-tui-components-test --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add cli/src/tui cli/tests/test_tui_components.cpp tests/CMakeLists.txt
git commit -m "feat: render reasoning and turn summaries in tui"
```

## Task 3: Session Model Override API

**Files:**
- Modify: `libs/storage/include/merak/session_store.hpp`
- Modify: `libs/storage/src/session_store.cpp`
- Modify: `libs/runtime/include/merak/runtime_service.hpp`
- Modify: `libs/runtime/src/runtime_service.cpp`
- Modify: `libs/http/include/merak/http_server.hpp`
- Modify: `libs/http/src/http_server.cpp`
- Modify: `cli/src/client/runtime_client.hpp`
- Modify: `cli/src/client/runtime_client.cpp`
- Modify: `cli/src/main.cpp`
- Test: `libs/storage/tests/test_storage.cpp`
- Test: `libs/http/tests/test_http.cpp`
- Test: `cli/tests/test_runtime_client.cpp`

- [ ] **Step 1: Write failing storage and HTTP tests**

Assert:

```cpp
store.set_session_model(session.id, "claude-sonnet");
assert(store.get_session(session.id)->model_override == "claude-sonnet");
```

and:

```cpp
auto models = server.handle_models();
assert(models.body["models"].size() == 2);
auto changed = server.handle_set_session_model(session_id, "model-b");
assert(changed.body["model"] == "model-b");
```

- [ ] **Step 2: Run focused tests and verify failure**

Run:

```bash
cmake --build build --target merak-storage-test merak-http-test -j
```

Expected: compile failure for missing override and model catalog APIs.

- [ ] **Step 3: Persist the override**

Add `model_override TEXT NOT NULL DEFAULT ''` through a backward-compatible
`ALTER TABLE` migration guarded against duplicate-column errors. Extend
`SessionRecord` and add:

```cpp
SessionRecord set_session_model(const std::string& id, const std::string& model);
```

- [ ] **Step 4: Add runtime and HTTP model APIs**

Pass configured `ModelEntry` values into runtime metadata. Add:

```text
GET  /v1/models
POST /v1/sessions/{id}/model
```

Reject unknown model names with `invalid_model`. The empty string clears the
override.

- [ ] **Step 5: Apply override to new loops**

Change `LoopFactory` to accept a selected model:

```cpp
using LoopFactory =
    std::function<std::unique_ptr<AgentLoop>(const std::string& model)>;
```

Resolve `session.model_override` before creating a loop. Default to configured
model when empty.

- [ ] **Step 6: Extend runtime client and run tests**

Add `models()` and `set_session_model(id, model)`, then run:

```bash
cmake --build build --target merak-storage-test merak-http-test merak-tui-client-test -j
ctest --test-dir build -R "merak-storage-test|merak-http-test|merak-tui-client-test" --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add libs/storage libs/runtime libs/http cli/src/client cli/src/main.cpp cli/tests
git commit -m "feat: add session model overrides"
```

## Task 4: Event Bridge, View Stack, Context Panel, And Model Picker

**Files:**
- Create: `cli/src/tui/runtime_event_bridge.hpp`
- Create: `cli/src/tui/view_stack.hpp`
- Modify: `cli/src/tui/components/status_bar.hpp`
- Modify: `cli/src/tui/composer/chat_composer.hpp`
- Modify: `cli/src/tui/terminal_event_reader.hpp`
- Modify: `cli/src/tui/screen_manager.hpp`
- Modify: `cli/src/main.cpp`
- Test: `cli/tests/test_tui_components.cpp`

- [ ] **Step 1: Write failing view and bridge tests**

Cover:

```cpp
ViewStack views;
views.push(View::Context);
assert(views.current() == View::Context);
views.pop();
assert(views.current() == View::Composer);
```

Feed bridge events for reasoning, answer, usage, tool completion, and
`run_completed`; assert timeline transitions and context counters.

- [ ] **Step 2: Add `ViewStack` and `RuntimeEventBridge`**

Use a compact enum:

```cpp
enum class View {
    Composer, Help, Context, ModelPicker, Transcript, ToolBrowser, ToolDetail
};
```

Move SSE type branching out of `main.cpp`. The bridge accepts callbacks for
remote actions while owning only UI mutation logic.

- [ ] **Step 3: Implement model picker and context panel**

Render configured model name, provider, and context limit. Context panel fields:

```text
Current prompt
Model limit
Cumulative input
Cumulative output
Messages
Completed turns
Tool calls
```

Unknown limits render `n/a`.

- [ ] **Step 4: Add retry shortcut**

Record submitted user inputs in `ChatComposer`. Map `Ctrl+R` in
`TerminalEventReader`; when idle and composer is empty, restore the most recent
user message without submitting it.

- [ ] **Step 5: Run TUI tests**

Run:

```bash
cmake --build build --target merak-tui-components-test merak-cli -j
ctest --test-dir build -R merak-tui-components-test --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add cli
git commit -m "feat: add tui event bridge and unified views"
```

## Task 5: Permission Policy And Workspace Rule Store

**Files:**
- Create: `libs/tools/include/merak/permission_policy.hpp`
- Create: `libs/tools/src/permission_policy.cpp`
- Modify: `libs/tools/CMakeLists.txt`
- Modify: `libs/tools/tests/test_tools.cpp`

- [ ] **Step 1: Write failing policy tests**

Cover:

```cpp
auto safe = policy.classify({"1", "write_file", R"({"path":"src/a.cpp"})"});
assert(safe.can_persist);
auto destructive = policy.classify({"2", "execute_bash", R"({"command":"rm -rf build"})"});
assert(destructive.risk_level == RiskLevel::High);
assert(!destructive.can_persist);
```

Write a rule, reload the store, and assert a matching call bypasses approval.
Write malformed JSON and assert loading returns an empty rule list plus a
warning string.

- [ ] **Step 2: Run focused tools test and verify failure**

Run:

```bash
cmake --build build --target merak-tools-test -j
```

Expected: compile failure because permission policy types do not exist.

- [ ] **Step 3: Implement policy types**

Define:

```cpp
enum class RiskLevel { Low, Medium, High };
struct ApprovalMetadata {
    std::string summary;
    RiskLevel risk_level;
    std::vector<std::string> risk_tags;
    std::string batch_group;
    std::filesystem::path workspace_root;
    bool can_persist = false;
};
```

Canonicalize workspace paths. Mark deletion commands, destructive Git
operations, and compound shell commands as non-persistable. Use structured JSON
parsing for tool arguments.

- [ ] **Step 4: Implement rule storage**

Store JSON at `<workspace>/.merak/permissions.json`. Write through a temporary
file and rename it. Match only tool name plus an optional canonical path scope.
Malformed files return no active rules and a warning.

- [ ] **Step 5: Run tests**

Run:

```bash
cmake --build build --target merak-tools-test -j
ctest --test-dir build -R merak-tools-test --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add libs/tools
git commit -m "feat: add workspace permission policy"
```

## Task 6: Runtime Approval Actions

**Files:**
- Modify: `libs/storage/include/merak/session_store.hpp`
- Modify: `libs/storage/src/session_store.cpp`
- Modify: `libs/runtime/include/merak/runtime_service.hpp`
- Modify: `libs/runtime/src/runtime_service.cpp`
- Modify: `libs/http/src/http_server.cpp`
- Modify: `libs/runtime/tests/test_runtime.cpp`
- Modify: `libs/http/tests/test_http.cpp`

- [ ] **Step 1: Write failing runtime approval tests**

Assert `approval_requested` includes metadata fields. Resolve with:

```json
{"action":"always_allow_workspace"}
```

and assert a safe rule is persisted. Assert high-risk approvals reject that
action with `unsafe_persistent_rule`. Assert `deny_group` resolves only entries
with the same batch group.

- [ ] **Step 2: Run focused tests and verify failure**

Run:

```bash
cmake --build build --target merak-runtime-test merak-http-test -j
```

Expected: compile or assertion failure for missing structured actions.

- [ ] **Step 3: Add structured approval actions**

Define:

```cpp
enum class ApprovalAction {
    AllowOnce, AlwaysAllowWorkspace, AllowGroup, Deny, DenyGroup
};
```

Persist approval metadata in SQLite. Keep the waiting control behavior, but
resolve all matching safe group entries for group actions. Rule hits bypass
approval before emitting `approval_requested`.

- [ ] **Step 4: Update HTTP approval resolution**

Accept an `action` string and return every resolved approval id. Preserve
`{"decision":"allow"}` and `{"decision":"deny"}` compatibility for older
clients.

- [ ] **Step 5: Run tests**

Run:

```bash
cmake --build build --target merak-runtime-test merak-http-test -j
ctest --test-dir build -R "merak-runtime-test|merak-http-test" --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add libs/storage libs/runtime libs/http
git commit -m "feat: support workspace and grouped approval actions"
```

## Task 7: TUI Approval Queue

**Files:**
- Create: `cli/src/tui/approval/approval_queue.hpp`
- Modify: `cli/src/tui/history_cell/history_cell.hpp`
- Modify: `cli/src/tui/components/status_bar.hpp`
- Modify: `cli/src/tui/screen_manager.hpp`
- Modify: `cli/src/client/runtime_client.hpp`
- Modify: `cli/src/client/runtime_client.cpp`
- Modify: `cli/src/tui/runtime_event_bridge.hpp`
- Modify: `cli/src/main.cpp`
- Test: `cli/tests/test_tui_components.cpp`
- Test: `cli/tests/test_runtime_client.cpp`

- [ ] **Step 1: Write failing queue tests**

Cover:

```cpp
ApprovalQueue queue;
queue.push(entry_a);
queue.push(entry_a_duplicate);
assert(queue.size() == 1);
queue.move_action_right();
queue.move_focus_down();
assert(queue.focused().approval_id == entry_b.approval_id);
```

Also assert high-risk entries disable workspace and group allows, while deny
group remains selectable.

- [ ] **Step 2: Run focused tests and verify failure**

Run:

```bash
cmake --build build --target merak-tui-components-test merak-tui-client-test -j
```

Expected: compile failure because queue APIs do not exist.

- [ ] **Step 3: Implement queue and rendering**

Deduplicate by request key from the server. Render the focused item, pending
count, summary, risk tags, and action row. Handle up/down, left/right, `Enter`,
`y`, `n`, and `Esc`. Send the selected structured action through
`RuntimeClient`.

- [ ] **Step 4: Connect status line and bridge**

Push entries on `approval_requested`, remove all ids listed by
`approval_resolved`, and expose pending count in `StatusBar`.

- [ ] **Step 5: Run tests**

Run:

```bash
cmake --build build --target merak-tui-components-test merak-tui-client-test merak-cli -j
ctest --test-dir build -R "merak-tui-components-test|merak-tui-client-test" --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add cli
git commit -m "feat: add complex approval queue to tui"
```

## Task 8: Full Verification And Documentation

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update user documentation**

Document streamed reasoning, `/model`, `/context`, `Ctrl+R`, approval queue
navigation, and workspace rules at `.merak/permissions.json`.

- [ ] **Step 2: Run formatting and diff checks**

Run:

```bash
git diff --check
```

Expected: no whitespace errors.

- [ ] **Step 3: Run the complete suite**

Run:

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 4: Smoke test the terminal client**

Run `merak serve` and `merak tui` in separate terminals. Confirm:

```text
reasoning streams separately from the final answer
/model changes the current session model
/context shows practical token counters
Ctrl+R restores the last user input
safe workspace rules persist
high-risk actions cannot be permanently allowed
```

- [ ] **Step 5: Commit**

```bash
git add README.md
git commit -m "docs: describe complete single-agent tui"
```

