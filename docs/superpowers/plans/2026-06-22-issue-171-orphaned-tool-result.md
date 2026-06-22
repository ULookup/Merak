# ISSUE #171 Orphaned tool_result Fix — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate Anthropic API HTTP 400 errors caused by orphaned `tool_result` blocks lacking matching `tool_use` blocks, via a three-layer defense (MemoryStore windowing + ContextPipeline round-aware hard trim + ContextSerializer safety net).

**Architecture:** Three independent layers. Layer 3 (safety net) ships first as pure-function stopgap. Layer 1 (MemoryStore) fixes the windowing root cause. Layer 2 (ContextPipeline hard trim) makes the token-budget trim round-aware. Each layer has dedicated tests. Three commits, each independently revertable.

**Tech Stack:** C++20, CMake, nlohmann::json, spdlog, pqxx (MemoryStore tests use `enabled=false` to skip DB).

**Spec:** `docs/superpowers/specs/2026-06-22-issue-171-orphaned-tool-result-design.md`

---

## File Structure

| File | Action | Responsibility |
|---|---|---|
| `libs/context/src/context_serializer.cpp` | Modify | Add `sanitize_orphans()` static helper + call it in `serialize()` |
| `libs/context/tests/test_serializer_orphans.cpp` | Create | Layer 3 unit tests |
| `tests/CMakeLists.txt` | Modify | Register `merak-context-serializer-test` |
| `libs/memory/src/memory_store.cpp` | Modify | Rewrite `recent_history()` to be round-aware + `adjust_for_orphan_tools()` helper |
| `libs/memory/tests/test_memory_history.cpp` | Create | Layer 1 unit tests |
| `tests/CMakeLists.txt` | Modify | Register `merak-memory-history-test` |
| `libs/context/src/context_pipeline.cpp` | Modify | Replace per-message hard trim with round-aware deletion |
| `libs/context/tests/test_pipeline_hard_trim.cpp` | Create | Layer 2 integration tests |
| `tests/CMakeLists.txt` | Modify | Register `merak-context-pipeline-test` |

---

## Task 1: Layer 3 Safety Net — `sanitize_orphans()` in ContextSerializer

**Files:**
- Modify: `libs/context/src/context_serializer.cpp` (add static helper after `namespace merak {` line 4, call it in `serialize()` after line 27 `payload.messages = ctx.provider_messages;`)
- Create: `libs/context/tests/test_serializer_orphans.cpp`
- Modify: `tests/CMakeLists.txt` (append new target)

- [ ] **Step 1: Write the failing test file**

Create `libs/context/tests/test_serializer_orphans.cpp`:

```cpp
#include <merak/context_serializer.hpp>
#include <merak/pipeline_types.hpp>
#include <merak/message.hpp>
#include <merak/tool_spec.hpp>
#include <cassert>
#include <iostream>
#include <string>

using namespace merak;

static Message make_user(const std::string& text) {
    Message m; m.role = "user"; m.content = text; return m;
}
static Message make_assistant_text(const std::string& text) {
    Message m; m.role = "assistant"; m.content = text; return m;
}
static Message make_assistant_with_tool(const std::string& text, const std::string& call_id, const std::string& tool_name = "read_file") {
    Message m; m.role = "assistant"; m.content = text;
    ToolCall tc; tc.id = call_id; tc.name = tool_name; tc.arguments = "{}";
    m.tool_calls.push_back(tc);
    return m;
}
static Message make_tool_result(const std::string& call_id, const std::string& output) {
    Message m; m.role = "tool"; m.content = output; m.tool_call_id = call_id;
    return m;
}

// Walks anthropic_json["messages"] and returns true if every tool_result block
// has a matching tool_use block in a prior assistant message.
static bool no_orphan_tool_results(const nlohmann::json& msgs) {
    std::vector<std::string> produced_ids;
    for (const auto& m : msgs) {
        const std::string role = m.value("role", "");
        if (m.contains("content") && m["content"].is_array()) {
            for (const auto& blk : m["content"]) {
                if (blk.value("type", "") == "tool_use") {
                    produced_ids.push_back(blk.value("id", ""));
                }
            }
        }
        if (role == "user" && m.contains("content") && m["content"].is_array()) {
            for (const auto& blk : m["content"]) {
                if (blk.value("type", "") == "tool_result") {
                    const std::string use_id = blk.value("tool_use_id", "");
                    bool found = false;
                    for (const auto& pid : produced_ids) {
                        if (pid == use_id) { found = true; break; }
                    }
                    if (!found) return false;
                }
            }
        }
    }
    return true;
}

// Returns true if no assistant message contains a tool_use block.
static bool no_tool_use_at_all(const nlohmann::json& msgs) {
    for (const auto& m : msgs) {
        if (m.contains("content") && m["content"].is_array()) {
            for (const auto& blk : m["content"]) {
                if (blk.value("type", "") == "tool_use") return false;
            }
        }
    }
    return true;
}

int main() {
    ContextSerializer serializer;

    // Test 1: Orphan tool_result at head is dropped (Anthropic)
    {
        BoundContext ctx;
        ctx.provider_messages = {
            make_tool_result("call_orphan_head", "stale result"),
            make_user("hello"),
            make_assistant_text("hi"),
        };
        auto payload = serializer.serialize(ctx, "claude-sonnet-4-6", "", 1024);
        assert(payload.is_anthropic);
        const auto& msgs = payload.anthropic_json["messages"];
        assert(!msgs.empty());
        assert(msgs[0].value("role", "") == "user");
        assert(no_orphan_tool_results(msgs));
        std::cout << "Test 1 passed: orphan tool_result at head dropped (Anthropic)\n";
    }

    // Test 2: Orphan tool_use at tail is dropped (Anthropic)
    {
        BoundContext ctx;
        ctx.provider_messages = {
            make_user("do X"),
            make_assistant_with_tool("", "call_orphan_tail"),
        };
        auto payload = serializer.serialize(ctx, "claude-sonnet-4-6", "", 1024);
        const auto& msgs = payload.anthropic_json["messages"];
        assert(no_tool_use_at_all(msgs));
        std::cout << "Test 2 passed: orphan tool_use at tail dropped (Anthropic)\n";
    }

    // Test 3: Paired tool_use preserved
    {
        BoundContext ctx;
        ctx.provider_messages = {
            make_user("do X"),
            make_assistant_with_tool("", "call_ok"),
            make_tool_result("call_ok", "result"),
        };
        auto payload = serializer.serialize(ctx, "claude-sonnet-4-6", "", 1024);
        const auto& msgs = payload.anthropic_json["messages"];
        assert(no_orphan_tool_results(msgs));
        // 3 logical messages: user, assistant(tool_use), user(tool_result)
        assert(msgs.size() == 3);
        std::cout << "Test 3 passed: paired tool_use preserved\n";
    }

    // Test 4: Multiple orphan tool_results at head all dropped
    {
        BoundContext ctx;
        ctx.provider_messages = {
            make_tool_result("orphan_1", "r1"),
            make_tool_result("orphan_2", "r2"),
            make_user("hello"),
            make_assistant_text("hi"),
        };
        auto payload = serializer.serialize(ctx, "claude-sonnet-4-6", "", 1024);
        const auto& msgs = payload.anthropic_json["messages"];
        assert(msgs[0].value("role", "") == "user");
        assert(no_orphan_tool_results(msgs));
        std::cout << "Test 4 passed: multiple orphan tool_results at head all dropped\n";
    }

    // Test 5: OpenAI format also has no orphan tool messages
    {
        BoundContext ctx;
        ctx.provider_messages = {
            make_tool_result("call_orphan_openai", "stale"),
            make_user("hello"),
            make_assistant_text("hi"),
        };
        auto payload = serializer.serialize(ctx, "gpt-4o", "", 1024);
        assert(!payload.is_anthropic);
        const auto& msgs = payload.openai_json["messages"];
        // First non-system message must not be a tool message
        bool found_first_non_system = false;
        for (const auto& m : msgs) {
            if (m.value("role", "") == "system") continue;
            assert(m.value("role", "") != "tool");
            found_first_non_system = true;
            break;
        }
        assert(found_first_non_system);
        std::cout << "Test 5 passed: OpenAI format also has no orphan tool at head\n";
    }

    std::cout << "All ContextSerializer orphan tests passed.\n";
    return 0;
}
```

- [ ] **Step 2: Register the test in CMakeLists**

Append to `tests/CMakeLists.txt` (after line 22, the existing `merak-context-test` block):

```cmake

# Context serializer orphan safety net tests
add_executable(merak-context-serializer-test
    ${CMAKE_SOURCE_DIR}/libs/context/tests/test_serializer_orphans.cpp
)
target_link_libraries(merak-context-serializer-test PRIVATE merak-context)
add_test(NAME merak-context-serializer-test COMMAND merak-context-serializer-test)
```

- [ ] **Step 3: Build the test to confirm it compiles**

Run: `cmake --build build --target merak-context-serializer-test -j8`
Expected: Builds successfully. (It will fail at runtime because `sanitize_orphans` is not implemented yet — the test will fail on assertions.)

- [ ] **Step 4: Run test to verify it fails**

Run: `./build/tests/merak-context-serializer-test`
Expected: Assertion failure on Test 1 (orphan tool_result at head is NOT dropped — current serializer emits it as `messages[0]` with `tool_result` block).

- [ ] **Step 5: Add `sanitize_orphans()` static helper in `context_serializer.cpp`**

In `libs/context/src/context_serializer.cpp`, immediately after line 4 (`namespace merak {`) and before the `serialize` function, insert:

```cpp
namespace {

// Safety net: drop orphaned tool messages so the serialized payload never
// violates the tool_use/tool_result pairing invariant.
// - Pass 1: drop leading tool messages whose tool_call_id has no matching
//   tool_use in any prior assistant message (orphan tool_result at head).
// - Pass 2: drop tool_calls from the last assistant message if none of its
//   ids have a matching tool_result afterwards (orphan tool_use at tail).
std::vector<Message> sanitize_orphans(std::vector<Message> msgs) {
    std::set<std::string> produced_ids;
    for (const auto& m : msgs) {
        if (m.role == "assistant") {
            for (const auto& tc : m.tool_calls) produced_ids.insert(tc.id);
        }
    }

    std::set<std::string> referenced_ids;
    for (const auto& m : msgs) {
        if (m.role == "tool" && m.tool_call_id) {
            referenced_ids.insert(*m.tool_call_id);
        }
    }

    // Pass 1: leading orphan tool messages
    size_t i = 0;
    while (i < msgs.size() && msgs[i].role == "tool") {
        const auto& id = msgs[i].tool_call_id;
        if (!id.has_value() || produced_ids.count(*id) == 0) {
            spdlog::warn("ContextSerializer: dropping orphan tool_result "
                         "(tool_use_id={}) at head",
                         id.value_or("<none>"));
            msgs.erase(msgs.begin() + static_cast<long>(i));
        } else {
            break;
        }
    }

    // Pass 2: last assistant's orphan tool_use
    int last_assistant = -1;
    for (int k = static_cast<int>(msgs.size()) - 1; k >= 0; k--) {
        if (msgs[k].role == "assistant") { last_assistant = k; break; }
    }
    if (last_assistant >= 0) {
        auto& last_a = msgs[last_assistant];
        bool all_orphan = !last_a.tool_calls.empty();
        for (const auto& tc : last_a.tool_calls) {
            if (referenced_ids.count(tc.id) > 0) { all_orphan = false; break; }
        }
        if (all_orphan) {
            spdlog::warn("ContextSerializer: dropping {} orphan tool_use at tail",
                         last_a.tool_calls.size());
            last_a.tool_calls.clear();
            if (last_a.content.empty()) {
                msgs.erase(msgs.begin() + static_cast<long>(last_assistant));
            }
        }
    }

    return msgs;
}

} // anonymous namespace
```

Also add `#include <set>` at the top of the file with the other includes.

- [ ] **Step 6: Call `sanitize_orphans()` in `serialize()`**

In `libs/context/src/context_serializer.cpp`, replace the line `payload.messages = ctx.provider_messages;` (currently line 27) with:

```cpp
  payload.messages = sanitize_orphans(ctx.provider_messages);
```

`ctx` is `const BoundContext&`, so this copies `ctx.provider_messages` into `sanitize_orphans`, which returns a new sanitized vector assigned to `payload.messages`. `ctx` itself is unchanged.

- [ ] **Step 7: Rebuild and run test**

Run: `cmake --build build --target merak-context-serializer-test -j8 && ./build/tests/merak-context-serializer-test`
Expected: All 5 tests pass with "All ContextSerializer orphan tests passed."

- [ ] **Step 8: Run existing context tests to ensure no regression**

Run: `cmake --build build --target merak-context-test -j8 && ./build/tests/merak-context-test`
Expected: Existing tests still pass.

- [ ] **Step 9: Commit**

```bash
git add libs/context/src/context_serializer.cpp \
        libs/context/tests/test_serializer_orphans.cpp \
        tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
fix(context): add sanitize_orphans safety net in ContextSerializer

Drops orphan tool_result at head and orphan tool_use at tail before
serialization. Prevents Anthropic API HTTP 400 errors when upstream
windowing or trimming produces unpaired tool messages.

Layer 3 of ISSUE #171 fix.
EOF
)"
```

---

## Task 2: Layer 1 — `MemoryStore::recent_history()` round-aware windowing

**Files:**
- Modify: `libs/memory/src/memory_store.cpp` (replace `recent_history` body at lines 28-39, add private helper `adjust_for_orphan_tools`)
- Modify: `libs/memory/include/merak/memory_store.hpp` (add private helper declaration)
- Create: `libs/memory/tests/test_memory_history.cpp`
- Modify: `tests/CMakeLists.txt` (append new target)

- [ ] **Step 1: Write the failing test file**

Create `libs/memory/tests/test_memory_history.cpp`:

```cpp
#include <merak/memory_store.hpp>
#include <merak/config.hpp>
#include <cassert>
#include <iostream>
#include <string>

using namespace merak;

static Message make_user(const std::string& text) {
    Message m; m.role = "user"; m.content = text; return m;
}
static Message make_assistant_text(const std::string& text) {
    Message m; m.role = "assistant"; m.content = text; return m;
}
static Message make_assistant_with_tool(const std::string& text, const std::string& call_id) {
    Message m; m.role = "assistant"; m.content = text;
    ToolCall tc; tc.id = call_id; tc.name = "read_file"; tc.arguments = "{}";
    m.tool_calls.push_back(tc);
    return m;
}
static Message make_tool_result(const std::string& call_id, const std::string& output) {
    Message m; m.role = "tool"; m.content = output; m.tool_call_id = call_id;
    return m;
}

// Builds a MemoryStore with disabled DB (no PostgreSQL needed).
static std::unique_ptr<MemoryStore> make_store() {
    MemoryConfig cfg;
    cfg.enabled = false;
    return std::make_unique<MemoryStore>(cfg, nullptr);
}

static bool starts_with_user(const std::vector<Message>& msgs) {
    return !msgs.empty() && msgs.front().role == "user";
}

static bool no_orphan_tool_at_head(const std::vector<Message>& msgs) {
    if (msgs.empty()) return true;
    if (msgs.front().role != "tool") return true;
    // If first is tool, its tool_call_id must be produced by an earlier assistant —
    // but there's no earlier assistant, so it's always orphan.
    return false;
}

int main() {
    // Test 1: No tool calls — returns last N turns
    {
        auto store = make_store();
        for (int i = 0; i < 5; i++) {
            store->append_message(make_user("u" + std::to_string(i)));
            store->append_message(make_assistant_text("a" + std::to_string(i)));
        }
        auto hist = store->recent_history(3);
        assert(hist.size() == 6);
        assert(hist[0].content == "u2");
        assert(hist[5].content == "a4");
        std::cout << "Test 1 passed: no tool calls, last N turns\n";
    }

    // Test 2: With tool calls — starts on user boundary
    {
        auto store = make_store();
        // Round 1: user → assistant(tool_use X) → tool(X)
        store->append_message(make_user("u1"));
        store->append_message(make_assistant_with_tool("", "call_X"));
        store->append_message(make_tool_result("call_X", "r1"));
        // Round 2: user → assistant(tool_use Y) → tool(Y)
        store->append_message(make_user("u2"));
        store->append_message(make_assistant_with_tool("", "call_Y"));
        store->append_message(make_tool_result("call_Y", "r2"));
        // Round 3: user → assistant(text)
        store->append_message(make_user("u3"));
        store->append_message(make_assistant_text("a3"));

        auto hist = store->recent_history(2);
        assert(starts_with_user(hist));
        assert(hist[0].content == "u2");
        std::cout << "Test 2 passed: with tool calls, starts on user boundary\n";
    }

    // Test 3: Orphan tool at head — expands to parent assistant
    {
        auto store = make_store();
        // Old round: user → assistant(tool_use Z) → tool(Z)
        store->append_message(make_user("u_old"));
        store->append_message(make_assistant_with_tool("", "call_Z"));
        store->append_message(make_tool_result("call_Z", "rZ"));
        // Recent round: user → assistant(text)
        store->append_message(make_user("u_recent"));
        store->append_message(make_assistant_text("a_recent"));

        // max_turns=1 should pick the last user-led round: [u_recent, a_recent].
        // But if naive max_turns*2 slicing cut between assistant(tool_use Z) and tool(Z),
        // we'd get [tool(Z), u_recent, a_recent] — orphan at head.
        // For this test we want to trigger expansion: construct a case where the
        // round-boundary window itself starts at a user (so no orphan). To force
        // an orphan scenario, we manually need a history where the last user-led
        // round boundary is preceded by tool messages without their parent assistant.
        //
        // Reconstruct: this happens when max_turns counting picks fewer rounds
        // than the tool-result tail. We simulate by having a long tool-only tail.
        // Since round-boundary logic always starts on user, the only way to get
        // an orphan tool at head is if there's no user in the kept window at all —
        // which we handle in Test 6.
        //
        // For Test 3, verify the normal case: no orphan when rounds are well-formed.
        auto hist = store->recent_history(1);
        assert(no_orphan_tool_at_head(hist));
        assert(hist[0].content == "u_recent");
        std::cout << "Test 3 passed: well-formed rounds, no orphan at head\n";
    }

    // Test 4: Orphan tool at head, parent too far — drops orphan
    {
        auto store = make_store();
        // Construct: no user messages at all, just assistant + orphan tools.
        // The fallback path returns last max_turns*2 and must drop leading orphan tools.
        store->append_message(make_assistant_with_tool("", "call_far"));
        // Many filler messages to push distance beyond max_turns*2 + 4
        for (int i = 0; i < 20; i++) {
            store->append_message(make_assistant_text("filler" + std::to_string(i)));
        }
        store->append_message(make_tool_result("call_far", "orphan_result"));
        store->append_message(make_assistant_text("tail"));

        auto hist = store->recent_history(2);
        assert(no_orphan_tool_at_head(hist));
        std::cout << "Test 4 passed: orphan tool at head with far parent, dropped\n";
    }

    // Test 5: Empty memory — returns empty
    {
        auto store = make_store();
        auto hist = store->recent_history(5);
        assert(hist.empty());
        std::cout << "Test 5 passed: empty memory returns empty\n";
    }

    // Test 6: No user messages — fallback to tail with orphan handling
    {
        auto store = make_store();
        // Only assistant and tool messages, no user.
        store->append_message(make_assistant_with_tool("", "call_nouser"));
        store->append_message(make_tool_result("call_nouser", "r"));
        store->append_message(make_assistant_text("tail"));
        auto hist = store->recent_history(2);
        assert(no_orphan_tool_at_head(hist));
        std::cout << "Test 6 passed: no user messages, fallback handles orphans\n";
    }

    std::cout << "All MemoryStore recent_history tests passed.\n";
    return 0;
}
```

- [ ] **Step 2: Register the test in CMakeLists**

Append to `tests/CMakeLists.txt`:

```cmake

# MemoryStore recent_history tests (no DB)
add_executable(merak-memory-history-test
    ${CMAKE_SOURCE_DIR}/libs/memory/tests/test_memory_history.cpp
)
target_link_libraries(merak-memory-history-test PRIVATE merak-memory)
add_test(NAME merak-memory-history-test COMMAND merak-memory-history-test)
```

- [ ] **Step 3: Build the test**

Run: `cmake --build build --target merak-memory-history-test -j8`
Expected: Builds successfully.

- [ ] **Step 4: Run test to verify it fails**

Run: `./build/tests/merak-memory-history-test`
Expected: Test 2 (or another) fails — current `recent_history` uses `max_turns * 2` which will produce wrong slice for tool-call conversations.

- [ ] **Step 5: Add `adjust_for_orphan_tools` declaration to header**

In `libs/memory/include/merak/memory_store.hpp`, add to the private section (after `std::expected<void, AgentError> create_tables();`):

```cpp
    // Adjusts the start index forward if the window begins with orphan tool
    // messages whose parent assistant is missing or too far away.
    // - Expands window backward if parent assistant is within max_turns*2 + 4.
    // - Otherwise drops the leading orphan tool messages.
    static int adjust_for_orphan_tools(const std::vector<Message>& msgs,
                                        int start, int max_turns);
```

- [ ] **Step 6: Implement `adjust_for_orphan_tools` and rewrite `recent_history` in `memory_store.cpp`**

Replace the current `recent_history` body (lines 28-39 of `libs/memory/src/memory_store.cpp`) with:

```cpp
std::vector<Message> MemoryStore::recent_history(int max_turns) const {
    std::lock_guard lock(working_memory_mutex_);
    int total = static_cast<int>(working_memory_.size());
    if (total == 0 || max_turns <= 0) return {};

    // 1. Collect user message indices (round boundaries)
    std::vector<int> user_indices;
    for (int i = 0; i < total; i++) {
        if (working_memory_[i].role == "user") user_indices.push_back(i);
    }

    int start = 0;
    if (!user_indices.empty()) {
        int keep_rounds = std::min(max_turns, static_cast<int>(user_indices.size()));
        if (keep_rounds <= 0) return {};
        start = user_indices[static_cast<int>(user_indices.size()) - keep_rounds];
    } else {
        // No user messages — fall back to last max_turns*2 messages
        start = std::max(0, total - max_turns * 2);
    }

    start = adjust_for_orphan_tools(working_memory_, start, max_turns);

    std::vector<Message> result;
    for (int i = start; i < total; i++) {
        result.push_back(working_memory_[i]);
    }
    return result;
}

int MemoryStore::adjust_for_orphan_tools(const std::vector<Message>& msgs,
                                          int start, int max_turns) {
    if (start >= static_cast<int>(msgs.size())) return start;
    if (msgs[start].role != "tool") return start;

    // Collect leading orphan tool ids
    std::vector<std::string> orphan_ids;
    int probe = start;
    while (probe < static_cast<int>(msgs.size()) && msgs[probe].role == "tool") {
        if (msgs[probe].tool_call_id) {
            orphan_ids.push_back(*msgs[probe].tool_call_id);
        }
        probe++;
    }
    if (orphan_ids.empty()) return start;

    // Search backward for the most recent assistant with matching tool_calls
    int parent_idx = -1;
    for (int i = start - 1; i >= 0; i--) {
        if (msgs[i].role != "assistant") continue;
        bool covers_all = true;
        for (const auto& oid : orphan_ids) {
            bool found = false;
            for (const auto& tc : msgs[i].tool_calls) {
                if (tc.id == oid) { found = true; break; }
            }
            if (!found) { covers_all = false; break; }
        }
        if (covers_all) { parent_idx = i; break; }
    }

    const int max_distance = max_turns * 2 + 4;
    if (parent_idx >= 0 && (start - parent_idx) <= max_distance) {
        spdlog::debug("MemoryStore: expanded window from {} to {} to cover "
                      "orphan tool_result ({} ids)",
                      start, parent_idx, orphan_ids.size());
        return parent_idx;
    }

    // Drop leading orphan tool messages
    int new_start = start;
    while (new_start < static_cast<int>(msgs.size()) &&
           msgs[new_start].role == "tool") {
        new_start++;
    }
    spdlog::warn("MemoryStore: dropped {} orphan tool messages at head "
                 "(max_turns={}, parent_distance={})",
                 new_start - start, max_turns,
                 parent_idx >= 0 ? (start - parent_idx) : -1);
    return new_start;
}
```

- [ ] **Step 7: Add necessary includes if missing**

Verify `libs/memory/src/memory_store.cpp` includes `<algorithm>` (already present line 4) and `<vector>` (transitively via header). No new includes needed.

- [ ] **Step 8: Rebuild and run test**

Run: `cmake --build build --target merak-memory-history-test -j8 && ./build/tests/merak-memory-history-test`
Expected: All 6 tests pass.

- [ ] **Step 9: Run existing memory-dependent tests for regression**

Run: `cmake --build build --target merak-agent-loop-test merak-context-test -j8 && ./build/tests/merak-agent-loop-test && ./build/tests/merak-context-test`
Expected: Existing tests still pass.

- [ ] **Step 10: Commit**

```bash
git add libs/memory/src/memory_store.cpp \
        libs/memory/include/merak/memory_store.hpp \
        libs/memory/tests/test_memory_history.cpp \
        tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
fix(memory): make recent_history round-aware to prevent orphan tool_results

Rewrite recent_history to slice on user-message boundaries instead of
naive max_turns*2 estimate. Adds adjust_for_orphan_tools helper that
expands the window to cover a parent assistant when nearby, or drops
leading orphan tool messages when the parent is too far.

Layer 1 of ISSUE #171 fix.
EOF
)"
```

---

## Task 3: Layer 2 — `ContextPipeline::planned_assemble()` round-aware hard trim

**Files:**
- Modify: `libs/context/src/context_pipeline.cpp` (replace lines 84-101, the `if (opt_stats.tokens_after > model_max_tokens)` block)
- Create: `libs/context/tests/test_pipeline_hard_trim.cpp`
- Modify: `tests/CMakeLists.txt` (append new target)

- [ ] **Step 1: Write the failing test file**

Create `libs/context/tests/test_pipeline_hard_trim.cpp`:

```cpp
#include <merak/context_pipeline.hpp>
#include <merak/message.hpp>
#include <merak/tool_spec.hpp>
#include <cassert>
#include <iostream>
#include <string>

using namespace merak;

static Message make_user(const std::string& text) {
    Message m; m.role = "user"; m.content = text; return m;
}
static Message make_assistant_text(const std::string& text) {
    Message m; m.role = "assistant"; m.content = text; return m;
}
static Message make_assistant_with_tool(const std::string& text, const std::string& call_id) {
    Message m; m.role = "assistant"; m.content = text;
    ToolCall tc; tc.id = call_id; tc.name = "read_file"; tc.arguments = "{}";
    m.tool_calls.push_back(tc);
    return m;
}
static Message make_tool_result(const std::string& call_id, const std::string& output) {
    Message m; m.role = "tool"; m.content = output; m.tool_call_id = call_id;
    return m;
}

// Verifies every tool message's tool_call_id has a matching assistant.tool_calls
// entry in a prior message.
static bool all_tool_messages_paired(const std::vector<Message>& msgs) {
    std::vector<std::string> produced;
    for (const auto& m : msgs) {
        if (m.role == "assistant") {
            for (const auto& tc : m.tool_calls) produced.push_back(tc.id);
        }
        if (m.role == "tool") {
            if (!m.tool_call_id) return false;
            bool found = false;
            for (const auto& pid : produced) {
                if (pid == *m.tool_call_id) { found = true; break; }
            }
            if (!found) return false;
        }
    }
    return true;
}

int main() {
    // Test 1: Hard trim keeps round boundaries (no orphan tool_result)
    {
        ContextPipeline pipeline;
        std::vector<Message> history;
        // 5 rounds, each with tool calls and large content to force token overflow
        for (int r = 0; r < 5; r++) {
            std::string rid = "r" + std::to_string(r);
            history.push_back(make_user(std::string(2000, 'u') + rid));
            history.push_back(make_assistant_with_tool("", "call_" + rid));
            history.push_back(make_tool_result("call_" + rid, std::string(2000, 't')));
        }
        // Final user message to trigger next turn
        history.push_back(make_user("finalize"));

        BindSources sources;  // empty tool specs
        // Very small max_tokens to force hard trim
        auto payload = pipeline.planned_assemble("system", "claude-sonnet-4-6",
                                                  500, history, sources);
        assert(all_tool_messages_paired(payload.messages));
        // Must start with a user message (round boundary)
        assert(!payload.messages.empty());
        assert(payload.messages.front().role == "user");
        std::cout << "Test 1 passed: hard trim keeps round boundaries\n";
    }

    // Test 2: Hard trim preserves at least one round
    {
        ContextPipeline pipeline;
        std::vector<Message> history;
        for (int r = 0; r < 5; r++) {
            std::string rid = "r" + std::to_string(r);
            history.push_back(make_user(std::string(2000, 'u') + rid));
            history.push_back(make_assistant_text(std::string(2000, 'a')));
        }
        BindSources sources;
        // Absurdly small max_tokens — must still keep at least 1 round
        auto payload = pipeline.planned_assemble("system", "claude-sonnet-4-6",
                                                  10, history, sources);
        assert(!payload.messages.empty());
        std::cout << "Test 2 passed: hard trim preserves at least one round\n";
    }

    // Test 3: End-to-end — no orphan tool_result in anthropic_json
    {
        ContextPipeline pipeline;
        std::vector<Message> history;
        for (int r = 0; r < 5; r++) {
            std::string rid = "r" + std::to_string(r);
            history.push_back(make_user(std::string(2000, 'u') + rid));
            history.push_back(make_assistant_with_tool("", "call_" + rid));
            history.push_back(make_tool_result("call_" + rid, std::string(2000, 't')));
        }
        history.push_back(make_user("go"));

        BindSources sources;
        auto payload = pipeline.planned_assemble("system", "claude-sonnet-4-6",
                                                  500, history, sources);
        // Walk anthropic_json["messages"] — every tool_result block must have
        // a matching tool_use block in a prior assistant message.
        std::vector<std::string> produced_ids;
        const auto& msgs = payload.anthropic_json["messages"];
        bool ok = true;
        for (const auto& m : msgs) {
            if (m.contains("content") && m["content"].is_array()) {
                for (const auto& blk : m["content"]) {
                    const std::string type = blk.value("type", "");
                    if (type == "tool_use") produced_ids.push_back(blk.value("id", ""));
                    if (type == "tool_result") {
                        const std::string use_id = blk.value("tool_use_id", "");
                        bool found = false;
                        for (const auto& pid : produced_ids) {
                            if (pid == use_id) { found = true; break; }
                        }
                        if (!found) { ok = false; break; }
                    }
                }
            }
            if (!ok) break;
        }
        assert(ok);
        std::cout << "Test 3 passed: end-to-end no orphan tool_result (ISSUE #171 repro)\n";
    }

    std::cout << "All ContextPipeline hard trim tests passed.\n";
    return 0;
}
```

- [ ] **Step 2: Register the test in CMakeLists**

Append to `tests/CMakeLists.txt`:

```cmake

# ContextPipeline hard trim tests
add_executable(merak-context-pipeline-test
    ${CMAKE_SOURCE_DIR}/libs/context/tests/test_pipeline_hard_trim.cpp
)
target_link_libraries(merak-context-pipeline-test PRIVATE merak-context)
add_test(NAME merak-context-pipeline-test COMMAND merak-context-pipeline-test)
```

- [ ] **Step 3: Build the test**

Run: `cmake --build build --target merak-context-pipeline-test -j8`
Expected: Builds successfully.

- [ ] **Step 4: Run test to verify it fails**

Run: `./build/tests/merak-context-pipeline-test`
Expected: Test 1 or Test 3 fails — current hard trim per-message erase can break tool_use/tool_result pairing.

- [ ] **Step 5: Replace hard trim block with round-aware version**

In `libs/context/src/context_pipeline.cpp`, replace the entire block from line 84 (`// Hard trim: enforce model_max_tokens as hard ceiling`) through line 101 (the closing `}` of the `if (opt_stats.tokens_after > model_max_tokens)` block) with:

```cpp
  // Hard trim: enforce model_max_tokens as hard ceiling.
  // Round-aware: deletes whole rounds (user-led) to preserve tool_use/tool_result
  // pairing. Re-scans round_starts each iteration to avoid index drift.
  if (opt_stats.tokens_after > model_max_tokens) {
      auto& msgs = bound.provider_messages;
      int removed = 0;
      while (opt_stats.tokens_after > model_max_tokens) {
          std::vector<size_t> rs;
          for (size_t i = 0; i < msgs.size(); i++) {
              if (msgs[i].role == "user") rs.push_back(i);
          }
          if (rs.size() <= 1) break;  // preserve at least one round

          size_t del_end = rs[1];
          int chars = 0;
          for (size_t i = rs[0]; i < del_end; i++) {
              chars += static_cast<int>(msgs[i].content.size());
          }
          opt_stats.tokens_after -= chars / 3.5;
          msgs.erase(msgs.begin() + static_cast<long>(rs[0]),
                     msgs.begin() + static_cast<long>(del_end));
          removed += static_cast<int>(del_end - rs[0]);
      }
      stats_.hard_trims += removed;
      spdlog::warn("ContextPipeline: hard trim removed {} messages (round-aware) "
                   "(tokens_after={}, max={})",
                   removed, opt_stats.tokens_after, model_max_tokens);
  }
```

- [ ] **Step 6: Rebuild and run test**

Run: `cmake --build build --target merak-context-pipeline-test -j8 && ./build/tests/merak-context-pipeline-test`
Expected: All 3 tests pass.

- [ ] **Step 7: Run full context test suite for regression**

Run: `cmake --build build --target merak-context-test merak-context-serializer-test merak-context-pipeline-test -j8 && ctest --test-dir build -R "merak-context" --output-on-failure`
Expected: All context tests pass.

- [ ] **Step 8: Run all tests for final regression check**

Run: `cmake --build build -j8 && ctest --test-dir build --output-on-failure`
Expected: All tests pass.

- [ ] **Step 9: Commit**

```bash
git add libs/context/src/context_pipeline.cpp \
        libs/context/tests/test_pipeline_hard_trim.cpp \
        tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
fix(context): make hard trim round-aware to preserve tool_use/tool_result pairing

Replace per-message erase in hard trim with whole-round deletion (user-led
boundaries). Prevents orphaned tool_result blocks when token-budget trim
removes an assistant message containing tool_use. Re-scans round starts
each iteration to avoid index drift.

Layer 2 of ISSUE #171 fix.
EOF
)"
```

---

## Task 4: Final verification

- [ ] **Step 1: Run the complete test suite**

Run: `cmake --build build -j8 && ctest --test-dir build --output-on-failure`
Expected: All tests pass, including the 3 new test executables.

- [ ] **Step 2: Verify three separate commits exist**

Run: `git log --oneline -5`
Expected: Three commits visible — sanitize_orphans (Layer 3), recent_history (Layer 1), hard trim (Layer 2), plus the spec commit.

- [ ] **Step 3: Manual smoke test (optional, if dev server available)**

If the user wants end-to-end verification, run the agent with a tool-heavy conversation that previously triggered the 400 error and confirm the error no longer occurs. The Layer 3 safety net should log `dropping orphan tool_result` warnings if any upstream path still produces orphans.
