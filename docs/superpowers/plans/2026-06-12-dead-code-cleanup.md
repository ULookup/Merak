# Dead Code Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Clean up 16 dead code items — delete 5 valueless remnants, extract 1 duplicate, wire 7 complete-but-unused implementations, and migrate SessionStore from SQLite to PostgreSQL with checkpoint recovery.

**Architecture:** The work progresses in dependency order: first delete files that nothing depends on, then extract the shared helper, then wire simple call sites, then execute the SessionStore migration, and finally wire checkpoint recovery. WebUI type cleanup is independent and can run in parallel.

**Tech Stack:** C++23, PostgreSQL (libpqxx), React/TypeScript

---

## File Structure Map

| File | Action | Responsibility |
|------|--------|----------------|
| `libs/context/CMakeLists.txt` | Modify | Remove `tool_result_compactor.cpp` from build |
| `libs/context/include/merak/tool_result_compactor.hpp` | **Delete** | Deprecated class |
| `libs/context/src/tool_result_compactor.cpp` | **Delete** | Deprecated implementation |
| `libs/context/include/merak/cache_aware_context.hpp` | Modify | Remove `append()` declaration |
| `libs/context/src/cache_aware_context.cpp` | Modify | Remove `append()` definition; wire `will_cache_hit()` into ContextPipeline |
| `libs/context/include/merak/context_pipeline.hpp` | Modify | Accept `Compactor&` in constructor or planned_assemble for `compact_one_round` |
| `libs/context/src/context_pipeline.cpp` | Modify | Wire `will_cache_hit` stats; wire `escalate_for_recovery` call from planner |
| `libs/context/src/context_planner.cpp` | Modify | Call `escalate_for_recovery()` at AggressivePrune tier |
| `libs/context/src/context_optimizer.cpp` | Modify | Call `compact_one_round()` in `drop_rounds` |
| `libs/context/include/merak/compactor.hpp` | Modify | Expose `compact_one_round` publicly |
| `libs/memory/include/merak/memory_store.hpp` | Modify | Remove `db_conn_` member |
| `libs/loop/src/agent_loop.cpp` | Modify | Wire `classify_error` in LLM error path |
| `libs/loop/include/merak/turn_ingestor.hpp` | — | Already has `classify_error` declaration, no change needed |
| `libs/runtime/include/merak/runtime_service.hpp` | Modify | `SessionStore` → `shared_ptr<SessionStore>`; add `save_checkpoint` method |
| `libs/runtime/src/runtime_service.cpp` | Modify | PG construction; `destroy_session` → `unregister_session_world`; checkpoint save/load |
| `libs/storage/include/merak/session_store.hpp` | **Replace** | Delete SQLite header; rename PG header to this path |
| `libs/storage/include/merak/session_store_pg.hpp` | **Delete** | Renamed to `session_store.hpp` |
| `libs/storage/src/session_store.cpp` | **Replace** | Delete SQLite impl; rename PG impl to this path |
| `libs/storage/src/session_store_pg.cpp` | **Delete** | Renamed to `session_store.cpp` |
| `libs/storage/CMakeLists.txt` | Modify | Update source file references |
| `libs/runtime/include/merak/checkpoint.hpp` | — | Keep in runtime/; already used by renamed SessionStore |
| `libs/worldbuilding/include/merak/worldbuilding/ids.hpp` | Modify | Add `inline remove_all_no_throw` |
| `libs/worldbuilding/src/agent_store.cpp` | Modify | Remove local `remove_all_no_throw`; use shared version |
| `libs/worldbuilding/src/world_store.cpp` | Modify | Remove local `remove_all_no_throw`; use shared version |
| `libs/worldbuilding/src/scene_orchestrator.cpp` | Modify | Wire prompt loaders per AgentKind |
| `libs/worldbuilding/include/merak/worldbuilding/pipeline_manager.hpp` | Modify | Add `worlds_base_dir` to Dependencies |
| `libs/worldbuilding/src/pipeline_manager.cpp` | Modify | Wire `create_story_structure` in `init_state_for_world` |
| `libs/http/src/worldbuilding_http_handler.cpp` | Modify | Return `world_session_count` in world list endpoint |
| `cli/src/main.cpp` | Modify | Update SessionStore construction (PG conn); pass `worlds_base_dir` to PipelineManager |
| `webui/src/api/types.ts` | Modify | Delete `ReviewIssue`, `ReviewSummary`, `PipelineState` |

---

### Task 1: Delete ToolResultCompactor (deprecated)

**Files:**
- Delete: `libs/context/include/merak/tool_result_compactor.hpp`
- Delete: `libs/context/src/tool_result_compactor.cpp`
- Modify: `libs/context/CMakeLists.txt`

- [ ] **Step 1: Delete the header file**

```bash
rm libs/context/include/merak/tool_result_compactor.hpp
```

- [ ] **Step 2: Delete the implementation file**

```bash
rm libs/context/src/tool_result_compactor.cpp
```

- [ ] **Step 3: Remove from CMakeLists.txt**

Read `libs/context/CMakeLists.txt`, find and remove the line containing `src/tool_result_compactor.cpp`.

- [ ] **Step 4: Build and verify**

```bash
cd build && cmake --build . -j
```

Expected: Build passes with no `tool_result_compactor` reference errors.

- [ ] **Step 5: Commit**

```bash
git add libs/context/CMakeLists.txt
git add libs/context/include/merak/tool_result_compactor.hpp
git add libs/context/src/tool_result_compactor.cpp
git commit -m "chore: delete deprecated ToolResultCompactor

Replaced by ContextOptimizer::microcompact(). Zero callers, marked
deprecated in design docs."
```

---

### Task 2: Delete CacheAwareContext::append() (trivial wrapper)

**Files:**
- Modify: `libs/context/include/merak/cache_aware_context.hpp`
- Modify: `libs/context/src/cache_aware_context.cpp`

- [ ] **Step 1: Remove declaration from header**

In `libs/context/include/merak/cache_aware_context.hpp`, delete the line:
```cpp
static void append(std::vector<Message>& messages, const Message& new_msg);
```

- [ ] **Step 2: Remove definition from .cpp**

In `libs/context/src/cache_aware_context.cpp`, delete lines 37-42:
```cpp
void CacheAwareContext::append(
    std::vector<Message>& messages,
    const Message& new_msg
) {
    messages.push_back(new_msg);
}
```

- [ ] **Step 3: Build and verify**

```bash
cd build && cmake --build . -j
```

- [ ] **Step 4: Commit**

```bash
git add libs/context/include/merak/cache_aware_context.hpp
git add libs/context/src/cache_aware_context.cpp
git commit -m "chore: delete CacheAwareContext::append — trivial push_back wrapper, zero callers"
```

---

### Task 3: Delete MemoryStore::db_conn_ member (never initialized)

**Files:**
- Modify: `libs/memory/include/merak/memory_store.hpp`

- [ ] **Step 1: Remove member declaration**

In `libs/memory/include/merak/memory_store.hpp`, delete line 69:
```cpp
std::string db_conn_;
```

- [ ] **Step 2: Build and verify**

```bash
cd build && cmake --build . -j
```

- [ ] **Step 3: Commit**

```bash
git add libs/memory/include/merak/memory_store.hpp
git commit -m "chore: delete MemoryStore::db_conn_ — never initialized, never used"
```

---

### Task 4: Delete unused WebUI types

**Files:**
- Modify: `webui/src/api/types.ts`

- [ ] **Step 1: Remove ReviewIssue interface**

In `webui/src/api/types.ts`, delete the `ReviewIssue` interface and any sub-interfaces it depends on that have no other consumers. Check for `ReviewIssue` references first:

```bash
grep -rn 'ReviewIssue' webui/src/ --include="*.ts" --include="*.tsx"
```

If only defined (not imported elsewhere), delete the interface block.

- [ ] **Step 2: Remove ReviewSummary interface**

```bash
grep -rn 'ReviewSummary' webui/src/ --include="*.ts" --include="*.tsx"
```

If only defined, delete the interface block.

- [ ] **Step 3: Remove PipelineState interface**

```bash
grep -rn 'PipelineState' webui/src/ --include="*.ts" --include="*.tsx" | grep -v 'types.ts'
```

Verify: the only result should be from `PipelineNavigator.tsx` importing `getPipelineState` from `api/client.ts` (which returns `PipelineViewData`, not `PipelineState`). Delete `PipelineState`.

- [ ] **Step 4: Verify build**

```bash
cd webui && npx tsc --noEmit
```

- [ ] **Step 5: Commit**

```bash
git add webui/src/api/types.ts
git commit -m "chore(webui): delete unused types — ReviewIssue, ReviewSummary, PipelineState"
```

---

### Task 5: Extract remove_all_no_throw to shared location

**Files:**
- Modify: `libs/worldbuilding/include/merak/worldbuilding/ids.hpp`
- Modify: `libs/worldbuilding/src/agent_store.cpp`
- Modify: `libs/worldbuilding/src/world_store.cpp`

- [ ] **Step 1: Add inline function to ids.hpp**

In `libs/worldbuilding/include/merak/worldbuilding/ids.hpp`, add after the existing function declarations:

```cpp
#include <filesystem>

inline void remove_all_no_throw(const std::filesystem::path& path) noexcept {
    try { std::filesystem::remove_all(path); } catch (...) {}
}
```

- [ ] **Step 2: Remove duplicate from world_store.cpp**

In `libs/worldbuilding/src/world_store.cpp`, delete the function (currently around lines 70-73):
```cpp
void remove_all_no_throw(const std::filesystem::path& path) noexcept {
    try { std::filesystem::remove_all(path); } catch (...) {}
}
```

Confirm both call sites (`remove_all_no_throw(root)`) still compile — they now resolve to `ids.hpp`'s inline version.

- [ ] **Step 3: Remove duplicate from agent_store.cpp**

In `libs/worldbuilding/src/agent_store.cpp`, delete the function (currently around lines 100-103):
```cpp
void remove_all_no_throw(const std::filesystem::path& path) noexcept {
    try {
        std::filesystem::remove_all(path);
    } catch (...) {
    }
}
```

- [ ] **Step 4: Build and verify**

```bash
cd build && cmake --build . -j
```

- [ ] **Step 5: Commit**

```bash
git add libs/worldbuilding/include/merak/worldbuilding/ids.hpp
git add libs/worldbuilding/src/world_store.cpp
git add libs/worldbuilding/src/agent_store.cpp
git commit -m "refactor: extract remove_all_no_throw duplicate to ids.hpp"
```

---

### Task 6: Wire worldbuilding prompt loaders into SceneOrchestrator

**Files:**
- Modify: `libs/worldbuilding/src/scene_orchestrator.cpp`
- Include: `libs/worldbuilding/src/prompts/character.hpp`
- Include: `libs/worldbuilding/src/prompts/creative_director.hpp`
- Include: `libs/worldbuilding/src/prompts/domain_manager.hpp`

- [ ] **Step 1: Read SceneOrchestrator to find CharacterContextView construction**

Read `libs/worldbuilding/src/scene_orchestrator.cpp` and `libs/worldbuilding/include/merak/worldbuilding/scene_orchestrator.hpp` to find where `CharacterContextView` objects are built (likely a `prepare_scene` method).

- [ ] **Step 2: Add includes to scene_orchestrator.cpp**

```cpp
#include "prompts/character.hpp"
#include "prompts/creative_director.hpp"
#include "prompts/domain_manager.hpp"
```

- [ ] **Step 3: Wire loaders in the Agent-kind switch**

In the method that builds `CharacterContextView`, find the switch/case on `AgentKind`. Append loaded prompt to `behavior_constraints`:

```cpp
namespace fs = std::filesystem;
// Resolve prompts_dir relative to the executable's location
fs::path prompts_dir = config_.prompts_dir; // or derive from exe path
if (prompts_dir.empty()) prompts_dir = "config/prompts";

// Inside the AgentKind switch:
case AgentKind::God:
    ctx.behavior_constraints += "\n" + prompts::load_creative_director(prompts_dir);
    break;
case AgentKind::Individual:
case AgentKind::Group:
    ctx.behavior_constraints += "\n" + prompts::load_character_prompt(prompts_dir);
    break;
case AgentKind::MapManager:
case AgentKind::HistoryManager:
case AgentKind::MagicManager:
case AgentKind::FactionManager:
case AgentKind::RelationManager:
    ctx.behavior_constraints += "\n" + prompts::load_domain_manager_prompt(prompts_dir);
    break;
```

> Note: SceneOrchestrator needs a `prompts_dir` config. Add a field to its configuration struct and set it from the caller (`WorldbuildingService` or `cli/src/main.cpp`), defaulting to `exe_dir_path() / ".." / "config" / "prompts"`.

- [ ] **Step 4: Build and verify**

```bash
cd build && cmake --build . -j
```

- [ ] **Step 5: Commit**

```bash
git add libs/worldbuilding/src/scene_orchestrator.cpp
git add libs/worldbuilding/include/merak/worldbuilding/scene_orchestrator.hpp
git commit -m "feat: wire worldbuilding prompt loaders into SceneOrchestrator

load_character_prompt, load_creative_director, load_domain_manager_prompt
now inject behavior constraints per AgentKind during scene preparation."
```

---

### Task 7: Wire unregister_session_world and world_session_count

**Files:**
- Modify: `libs/runtime/src/runtime_service.cpp`
- Modify: `libs/http/src/worldbuilding_http_handler.cpp`

- [ ] **Step 1: Wire unregister_session_world in destroy_session**

Find `RuntimeService::destroy_session` (or the session cleanup path) in `libs/runtime/src/runtime_service.cpp`. At the end of the method, add:

```cpp
unregister_session_world(session_id);
```

If `destroy_session` doesn't exist, find where sessions are removed/destroyed and add the call there.

- [ ] **Step 2: Expose world_session_count in WorldbuildingHttpHandler**

In `libs/http/src/worldbuilding_http_handler.cpp`, find the world list endpoint handler. After fetching the world list, annotate each world with its active session count:

```cpp
// Inside the world list response builder:
auto* runtime = runtime_service_.lock(); // or however it's accessed
if (runtime) {
    world_json["active_sessions"] = runtime->world_session_count(world["id"]);
}
```

- [ ] **Step 3: Build and verify**

```bash
cd build && cmake --build . -j
```

- [ ] **Step 4: Commit**

```bash
git add libs/runtime/src/runtime_service.cpp
git add libs/http/src/worldbuilding_http_handler.cpp
git commit -m "feat: wire unregister_session_world on session destroy, expose session count in world list"
```

---

### Task 8: Wire escalate_for_recovery in ContextPlanner

**Files:**
- Modify: `libs/context/src/context_planner.cpp`
- Modify: `libs/context/src/context_pipeline.cpp`

- [ ] **Step 1: Pass ContextPipeline reference to ContextPlanner**

Since `ContextPlanner::plan()` is `const`, add a callback or pass `ContextPipeline*` to allow calling `escalate_for_recovery`. The simplest approach: add a `std::function<void()>` parameter to `PlanInput`:

In `libs/core/include/merak/pipeline_types.hpp`, add to `PlanInput`:
```cpp
std::function<void()> on_escalate;
```

- [ ] **Step 2: Call escalate from planner at AggressivePrune**

In `libs/context/src/context_planner.cpp`, in the `plan()` method, after `select_tier` determines the tier:

```cpp
if (tier == CompactionTier::AggressivePrune && input.on_escalate) {
    input.on_escalate();
}
```

- [ ] **Step 3: Wire the callback in ContextPipeline::planned_assemble**

In `libs/context/src/context_pipeline.cpp`, in `planned_assemble()`, when building `PlanInput`:

```cpp
PlanInput input{...};
input.on_escalate = [this]() { escalate_for_recovery(); };
```

- [ ] **Step 4: Extend escalate_for_recovery implementation**

In `libs/context/src/context_pipeline.cpp`, expand `escalate_for_recovery()`:

```cpp
void ContextPipeline::escalate_for_recovery() {
    // Trigger full spill before recording stats
    spill_store_.dump_all();
    stats_.record(ContextFeedback{0, 0, 0, 0, 0, true, false, true, 0}, OptimizeStats{});
}
```

Check if `spill_store_` has a `dump_all()` method. If not, use the existing spill interface.

- [ ] **Step 5: Build and verify**

```bash
cd build && cmake --build . -j
```

- [ ] **Step 6: Commit**

```bash
git add libs/context/src/context_planner.cpp
git add libs/context/src/context_pipeline.cpp
git add libs/core/include/merak/pipeline_types.hpp
git commit -m "feat: wire escalate_for_recovery — trigger spill at AggressivePrune tier"
```

---

### Task 9: Wire classify_error in AgentLoop LLM error path

**Files:**
- Modify: `libs/loop/src/agent_loop.cpp`

- [ ] **Step 1: Find the LLM call in run_loop**

In `libs/loop/src/agent_loop.cpp`, the LLM call is at `llm_->chat(req, ...)` followed by `llm_future.get()`. Wrap it in try-catch:

```cpp
std::future<ChatResponse> llm_future;
try {
    llm_future = llm_->chat(req,
        [&](StreamChunk chunk) { ... },
        control.cancellation_token());
} catch (const std::exception& e) {
    // Connection-level error
    auto error_class = turn_ingestor_.classify_error(0, e.what());
    if (error_class == LlmErrorClass::Auth) {
        throw AgentError(ErrorType::AUTH_ERROR, "LLM authentication failed. Check your API key.");
    }
    throw;
}

ChatResponse llm_response;
try {
    llm_response = llm_future.get();
} catch (const std::exception& e) {
    auto error_class = turn_ingestor_.classify_error(0, e.what());
    switch (error_class) {
    case LlmErrorClass::RateLimit:
        spdlog::warn("Loop: rate limited, retrying after backoff");
        std::this_thread::sleep_for(std::chrono::seconds(2));
        llm_future = llm_->chat(req, [&](StreamChunk) {}, control.cancellation_token());
        llm_response = llm_future.get(); // single retry
        break;
    case LlmErrorClass::ContextWindow:
        spdlog::warn("Loop: context window error, triggering compaction and retry");
        maybe_compact(control);
        llm_future = llm_->chat(req, [&](StreamChunk) {}, control.cancellation_token());
        llm_response = llm_future.get();
        break;
    default:
        throw;
    }
}
```

- [ ] **Step 2: Build and verify**

```bash
cd build && cmake --build . -j
```

- [ ] **Step 3: Commit**

```bash
git add libs/loop/src/agent_loop.cpp
git commit -m "feat: wire classify_error — LLM errors now classified for retry strategy"
```

---

### Task 10: Wire will_cache_hit into ContextPipeline stats

**Files:**
- Modify: `libs/context/src/context_pipeline.cpp`

- [ ] **Step 1: Add prev_split member to ContextPipeline**

In `libs/context/include/merak/context_pipeline.hpp`, add:
```cpp
private:
    std::optional<CacheAwareContext::Split> prev_split_;
```

- [ ] **Step 2: Call will_cache_hit in planned_assemble**

At the end of `ContextPipeline::planned_assemble()`, after the serializer produces the final payload:

```cpp
if (config_.enable_cache) {
    auto curr_split = CacheAwareContext::split(payload.messages); // or use the assembled messages
    if (prev_split_.has_value()) {
        bool hit = CacheAwareContext::will_cache_hit(*prev_split_, curr_split);
        stats_.record_cache_hit(hit);
    }
    prev_split_ = curr_split;
}
```

Check `PipelineStats` for an existing cache hit recording method; if none exists, add one:
```cpp
void record_cache_hit(bool hit) { cache_hits_ += hit ? 1 : 0; cache_checks_++; }
```

- [ ] **Step 3: Build and verify**

```bash
cd build && cmake --build . -j
```

- [ ] **Step 4: Commit**

```bash
git add libs/context/src/context_pipeline.cpp
git add libs/context/include/merak/context_pipeline.hpp
git add libs/context/include/merak/pipeline_stats.hpp
git commit -m "feat: wire will_cache_hit — track cache hit rate in PipelineStats"
```

---

### Task 11: Wire compact_one_round in ContextOptimizer::drop_rounds

**Files:**
- Modify: `libs/context/src/context_optimizer.cpp`
- Modify: `libs/context/include/merak/context_optimizer.hpp`

- [ ] **Step 1: Add Compactor dependency to ContextOptimizer**

In `libs/context/include/merak/context_optimizer.hpp`, add a setter for an optional compactor:
```cpp
void set_compactor(std::shared_ptr<class Compactor> compactor) { compactor_ = compactor; }
private:
    std::shared_ptr<class Compactor> compactor_;
```

Forward-declare `Compactor` at the top.

- [ ] **Step 2: Wire compact_one_round in drop_rounds**

In `libs/context/src/context_optimizer.cpp`, in `drop_rounds()`, before erasing the dropped rounds, compact them:

```cpp
// Before: history.erase(history.begin(), history.begin() + keep_from);
// Add:
if (compactor_ && drop_count > 0) {
    // Compact each dropped round into a summary
    std::vector<Message> dropped(history.begin(), history.begin() + keep_from);
    std::vector<Message> compacted;
    size_t round_idx = 0;
    for (size_t i = 0; i < static_cast<size_t>(drop_count) && round_idx < round_starts.size(); i++) {
        size_t start = round_starts[round_idx];
        size_t end = (round_idx + 1 < round_starts.size()) ? round_starts[round_idx + 1] : keep_from;
        std::vector<Message> round_msgs(dropped.begin() + start, dropped.begin() + end);
        auto summary_future = compactor_->compact_one_round(round_msgs);
        auto summary = summary_future.get();
        if (!summary.empty()) {
            compacted.push_back({"system", "[Compacted round " + std::to_string(i + 1) + "]: " + summary, {}, "", ""});
        }
        round_idx++;
    }
    // Replace dropped messages with compacted summaries
    history.erase(history.begin(), history.begin() + keep_from);
    history.insert(history.begin(), compacted.begin(), compacted.end());
} else {
    history.erase(history.begin(), history.begin() + keep_from);
}
```

- [ ] **Step 3: Wire compactor from ContextPipeline**

In `libs/context/src/context_pipeline.cpp`, in the constructor or `planned_assemble`, pass the compactor to the optimizer:

```cpp
optimizer_.set_compactor(compactor_);
```

Add `compactor_` as a member of `ContextPipeline` and a constructor parameter.

- [ ] **Step 4: Build and verify**

```bash
cd build && cmake --build . -j
```

- [ ] **Step 5: Commit**

```bash
git add libs/context/src/context_optimizer.cpp
git add libs/context/include/merak/context_optimizer.hpp
git add libs/context/src/context_pipeline.cpp
git add libs/context/include/merak/context_pipeline.hpp
git commit -m "feat: wire compact_one_round — dropped rounds now generate summaries instead of silent discard"
```

---

### Task 12: Migrate SessionStore from SQLite to PostgreSQL

**Files:**
- Delete: `libs/storage/include/merak/session_store.hpp` (SQLite)
- Delete: `libs/storage/src/session_store.cpp` (SQLite)
- Rename: `libs/storage/include/merak/session_store_pg.hpp` → `libs/storage/include/merak/session_store.hpp`
- Rename: `libs/storage/src/session_store_pg.cpp` → `libs/storage/src/session_store.cpp`
- Modify: `libs/storage/CMakeLists.txt`
- Modify: `libs/runtime/include/merak/runtime_service.hpp`
- Modify: `libs/runtime/src/runtime_service.cpp`
- Modify: `cli/src/main.cpp`

- [ ] **Step 1: Rename PG header, port missing methods from SQLite**

Read `libs/storage/include/merak/session_store_pg.hpp`. The SQLite `SessionStore` has two methods not in `SessionStorePG`:
- `std::filesystem::path journal_path(const std::string& session_id) const;`
- `void set_plan(const std::string& plan_text);`
- `std::optional<std::string> get_plan() const;`

Add these to the PG version with PostgreSQL implementations. For `set_plan`/`get_plan`, add a `plans` table to `initialize()`:
```sql
CREATE TABLE IF NOT EXISTS plans (id TEXT PRIMARY KEY, plan_text TEXT NOT NULL DEFAULT '');
```

For `journal_path`, since PG doesn't use file-based journals, return `merak_home() / "journal" / (session_id + ".jsonl")`.

Then:
```bash
mv libs/storage/include/merak/session_store_pg.hpp libs/storage/include/merak/session_store.hpp
```

- [ ] **Step 2: Rename PG implementation, rename class**

Read `libs/storage/src/session_store_pg.cpp`. Rename the class from `SessionStorePG` to `SessionStore`:
```bash
# In the renamed file, replace all SessionStorePG with SessionStore
sed -i '' 's/SessionStorePG/SessionStore/g' libs/storage/src/session_store_pg.cpp
```

Update the include at the top:
```cpp
#include <merak/session_store.hpp>  // was session_store_pg.hpp
```

Add implementations for `set_plan`, `get_plan`, and `journal_path`.

Then:
```bash
mv libs/storage/src/session_store_pg.cpp libs/storage/src/session_store.cpp
```

- [ ] **Step 3: Delete SQLite files**

```bash
rm libs/storage/include/merak/session_store.hpp  # the OLD SQLite one (already overwritten by mv above)
# Wait — mv already replaced it. The old SQLite header is gone.
rm libs/storage/src/session_store.cpp  # the OLD SQLite one (already overwritten)
```
> Note: The `mv` commands in Steps 1-2 overwrite the old SQLite files. Confirm the old SQLite `session_store.hpp` and `session_store.cpp` no longer exist as separate files.

- [ ] **Step 4: Update CMakeLists.txt**

In `libs/storage/CMakeLists.txt`, ensure the source file list references `src/session_store.cpp` (no longer references `session_store_pg.cpp` or any SQLite-specific file). Remove `sqlite3` from link dependencies if it was only used by SessionStore. (Check: other files in storage may still use SQLite.)

- [ ] **Step 5: Update RuntimeService**

In `libs/runtime/include/merak/runtime_service.hpp`, change the member:
```cpp
// Before:
SessionStore store_;
// After:
std::shared_ptr<SessionStore> store_;
```

Update the constructor in `libs/runtime/src/runtime_service.cpp`:
```cpp
// Before:
RuntimeService::RuntimeService(std::filesystem::path root, ...)
    : store_(root) { ... }
// After:
RuntimeService::RuntimeService(std::shared_ptr<SessionStore> store, ...)
    : store_(std::move(store)) { ... }
```

Update all `store_.method()` calls to `store_->method()`.

- [ ] **Step 6: Update cli/src/main.cpp**

In `cli/src/main.cpp`, construct SessionStore with a PG connection:

```cpp
// Before:
auto session_store = std::make_shared<SessionStore>(merak_home());

// After:
auto pg_conn = std::make_shared<pqxx::connection>(cfg.memory.db_connection);
auto session_store = std::make_shared<SessionStore>(pg_conn);
session_store->initialize(); // Note: RuntimeService::initialize() also calls this
```

Update `RuntimeService` construction to pass `session_store`:
```cpp
auto runtime = std::make_shared<RuntimeService>(merak_home(), factory, cfg.agent.sub_agents, sub_executor);
// becomes:
auto runtime = std::make_shared<RuntimeService>(session_store, factory, cfg.agent.sub_agents, sub_executor);
```

- [ ] **Step 7: Build and fix compilation errors**

```bash
cd build && cmake --build . -j
```

Fix any compilation errors from remaining SQLite references or type mismatches. Iterate until clean build.

- [ ] **Step 8: Commit**

```bash
git add libs/storage/
git add libs/runtime/
git add cli/src/main.cpp
git commit -m "feat: migrate SessionStore from SQLite to PostgreSQL

Delete SQLite SessionStore. Rename SessionStorePG → SessionStore.
Port set_plan/get_plan/journal_path from SQLite implementation.
RuntimeService now takes shared_ptr<SessionStore>."
```

---

### Task 13: Wire checkpoint save in AgentLoop via RuntimeService

**Files:**
- Modify: `libs/runtime/include/merak/runtime_service.hpp`
- Modify: `libs/runtime/src/runtime_service.cpp`
- Modify: `libs/loop/src/agent_loop.cpp`
- Modify: `libs/core/include/merak/execution.hpp` (add checkpoint callback to RunControl)

- [ ] **Step 1: Add save_checkpoint to RunControl**

In `libs/core/include/merak/execution.hpp`, find `RunControl`. Add a callback:
```cpp
std::function<void(int turn_index, const std::string& turn_state_json,
                   int64_t input_tokens, int64_t output_tokens,
                   const std::string& pending_calls_json,
                   const std::string& compacted_summary,
                   const std::string& pipeline_snapshot_json)> save_checkpoint;
```

- [ ] **Step 2: Wire checkpoint in AgentLoop::run_loop**

In `libs/loop/src/agent_loop.cpp`, at the end of each turn (after tool calls complete, before next iteration), serialize current state and call the checkpoint callback:

```cpp
// After handle_tool_calls returns, before the next while loop iteration:
if (control.save_checkpoint) {
    nlohmann::json turn_state;
    turn_state["state"] = state_name(state_);
    turn_state["turn_count"] = turn_count;
    control.save_checkpoint(
        current_turn_,
        turn_state.dump(),
        llm_response.total_input_tokens,
        llm_response.total_output_tokens,
        "[]", // pending calls cleared after this turn
        "",   // compacted summary (filled by maybe_compact)
        ""    // pipeline snapshot (filled by ContextPipeline)
    );
}
```

- [ ] **Step 3: Wire callback from RuntimeService::Control**

Find `RuntimeService::Control` (inner class in `runtime_service.cpp`). In its constructor, set `save_checkpoint`:
```cpp
control->save_checkpoint = [this, run_id](int turn_index, ...) {
    store_->save_checkpoint(
        make_id("ckpt"), run_id, turn_index, turn_state_json,
        input_tokens, output_tokens, pending_calls_json,
        compacted_summary, pipeline_snapshot_json);
};
```

- [ ] **Step 4: Wire checkpoint recovery in resume_after_restarted_approval**

In `libs/runtime/src/runtime_service.cpp`, find `resume_after_restarted_approval()`. Before restoring messages from events, try loading the latest checkpoint:
```cpp
auto ckpt_json = store_->load_latest_checkpoint_json(run.id);
if (ckpt_json.has_value()) {
    auto ckpt = nlohmann::json::parse(*ckpt_json);
    auto turn_state_json = ckpt["turn_state"];
    // Restore TurnState from serialized data
    // ...
}
```

- [ ] **Step 5: Build and verify**

```bash
cd build && cmake --build . -j
```

- [ ] **Step 6: Commit**

```bash
git add libs/runtime/
git add libs/loop/src/agent_loop.cpp
git add libs/core/include/merak/execution.hpp
git commit -m "feat: wire checkpoint save/recovery — crash-resilient AgentLoop"
```

---

### Task 14: Wire create_story_structure in PipelineManager

**Files:**
- Modify: `libs/worldbuilding/include/merak/worldbuilding/pipeline_manager.hpp`
- Modify: `libs/worldbuilding/src/pipeline_manager.cpp`
- Modify: `cli/src/main.cpp`

- [ ] **Step 1: Add worlds_base_dir to Dependencies**

In `libs/worldbuilding/include/merak/worldbuilding/pipeline_manager.hpp`, add to `Dependencies`:
```cpp
std::filesystem::path worlds_base_dir;
```

- [ ] **Step 2: Wire create_story_structure in init_state_for_world**

In `libs/worldbuilding/src/pipeline_manager.cpp`, in `init_state_for_world()`, after creating the fresh state (around line 235), check and create story structure:

```cpp
// After: worlds_[world_id] = WorldEntry{state, wf->name};
// Add:
auto story_path = deps_.worlds_base_dir / world_id / "story_structure.json";
if (!std::filesystem::exists(story_path)) {
    std::filesystem::create_directories(story_path.parent_path());
    nlohmann::json structure;
    structure["template"] = "three_act";
    structure["stages"] = {"建立", "对抗", "解决"};
    std::ofstream out(story_path);
    out << structure.dump(2);
}
```

> Note: This is a simplified inline version since `init_state_for_world` doesn't have `NarrativeStore` access. A fuller implementation would call `NarrativeStore::create_story_structure()`, but that requires threading `NarrativeStore&` through Dependencies.

- [ ] **Step 3: Pass worlds_base_dir from main.cpp**

In `cli/src/main.cpp`, when constructing `PipelineManager::Dependencies`:
```cpp
.worlds_base_dir = merak_home() / "worlds",
```

- [ ] **Step 4: Build and verify**

```bash
cd build && cmake --build . -j
```

- [ ] **Step 5: Commit**

```bash
git add libs/worldbuilding/
git add cli/src/main.cpp
git commit -m "feat: wire create_story_structure — auto-create default story structure on world init"
```

---

### Task 15: Final build verification and integration test

- [ ] **Step 1: Full clean build**

```bash
cd build && cmake --build . -j
```

Expected: zero errors, zero warnings.

- [ ] **Step 2: Run C++ tests**

```bash
cd build && ctest --output-on-failure
```

- [ ] **Step 3: Run WebUI type check**

```bash
cd webui && npx tsc --noEmit
```

- [ ] **Step 4: Serve smoke test**

Start `merak serve` with a valid PostgreSQL connection and a valid API key. Verify:
- Server starts without errors
- `POST /v1/sessions` creates a session
- `POST /v1/sessions/{id}/runs` starts a run
- SSE events stream correctly

- [ ] **Step 5: Commit any fixups**

```bash
git add -A
git commit -m "chore: final build fixes and integration test pass"
```
