# Design: PR #67 CRITICAL + IMPORTANT Fixes

**Date:** 2026-06-08
**Scope:** 2 CRITICAL + 21 IMPORTANT issues from code review of PR #67
**Branch:** `fix/webui-approval-inspector-fixes`
**Target:** `main`

## Summary

Fix 23 issues (2 CRITICAL, 21 IMPORTANT) found during strict code review of PR #67.
Organized into 4 independent commits by module.
Total estimated diff: ~250 lines across ~15 files.

## Commit 1: Version Conflict Fix + Atomic Write (`fix: version-conflict-and-atomic-write`)

**Fixes:** C1, TOCTOU (security finding #2), call_id regression (I2)
**Files:** `libs/worldbuilding/src/agent_store.cpp`, `libs/worldbuilding/src/worldbuilding_tools.cpp`

### 1a. `AgentStore::patch_character_card` — skip version check when version=0

**File:** `libs/worldbuilding/src/agent_store.cpp` (line 552)

**Problem:** `expected_version=0` always triggers `VersionConflictError` because
`current.version >= 1` for all created cards. The intent (per comment on line 1741 of
worldbuilding_tools.cpp) is that version=0 means "no version check."

**Fix:**
```cpp
// Line 552: add version > 0 guard
if (expected_version > 0 && current.version != expected_version) {
    throw VersionConflictError(current.version);
}

// Line 585: version increment adapts to version=0 case
current.version = (expected_version > 0) ? expected_version + 1 : current.version + 1;
```

### 1b. Reorder write operations — DB first, then files

**Problem:** File writes happen before DB updates with no transaction boundary.
If DB update fails after file write, state is inconsistent.

**Fix:** Reorder so DB UPDATE executes first with version guard in WHERE clause.
Only write files after DB confirms success.

```cpp
// After field modifications, before file writes:
PgConn conn(*pool_);

// UPDATE with version guard
int affected = conn.execute(
    "UPDATE character_cards SET ... WHERE agent_id = $18 AND version = $19",
    {..., std::to_string(expected_version)});

if (affected == 0) {
    throw VersionConflictError(expected_version);  // DB-level conflict detection
}

// Only now write files
write_text(root / "character_card.md", markdown);
write_text(root / "character_card_history" / history_filename(...), markdown);
```

### 1c. `UpdateCharacterCardTool` — catch VersionConflictError specifically

**File:** `libs/worldbuilding/src/worldbuilding_tools.cpp` (line 1749)

**Problem:** `VersionConflictError` is caught by generic `catch (std::exception&)` and
converted to unstructured INTERNAL error. Caller loses version conflict information.

**Fix:** Add specific catch before generic catch:
```cpp
} catch (const worldbuilding::VersionConflictError& e) {
    result.is_error = true;
    result.output = error_response(ToolErrorCode::VERSION_CONFLICT,
        "卡片已被其他来源修改（当前版本：" + std::to_string(e.current_version) + "），请刷新后重试");
} catch (const std::exception& e) {
    // existing generic handler
}
```

### 1d. `UpdateAgentPromptTool` — set call_id

**File:** `libs/worldbuilding/src/worldbuilding_tools.cpp` (lines 1671, 1686)

**Problem:** Both success and error paths create `ToolResult` without setting `call_id`.
Downstream correlation of tool results with tool calls is broken.

**Fix:** Add `r.call_id = call.id;` on both paths.

---

## Commit 2: DSL Renderer Cascading Replacement (`fix: dsl-renderer-cascading-replacement`)

**Fixes:** C2
**File:** `libs/context_dsl/src/renderer.cpp`

### Problem

`Renderer::render` does sequential find-and-replace. If resolved content for reference A
contains the raw text pattern of reference B, the subsequent replacement step incorrectly
matches and replaces it. Cascading or overlapping replacements corrupt template output.

### Fix: Two-pass placeholder substitution

```cpp
std::string Renderer::render(const std::string& template_text,
                              const std::vector<ResolvedContent>& resolved) {
    std::string result = template_text;

    // Pass 1: replace each @xxx{...} with a unique placeholder
    std::vector<std::string> placeholders;
    placeholders.reserve(resolved.size());
    for (size_t i = 0; i < resolved.size(); ++i) {
        std::string ph = "\x00DSL:" + std::to_string(i) + "\x00";
        placeholders.push_back(ph);
        auto pos = result.find(resolved[i].ref_raw);
        if (pos != std::string::npos) {
            result.replace(pos, resolved[i].ref_raw.length(), ph);
        }
    }

    // Pass 2: replace each placeholder with the rendered content
    for (size_t i = 0; i < resolved.size(); ++i) {
        auto pos = result.find(placeholders[i]);
        if (pos != std::string::npos) {
            result.replace(pos, placeholders[i].length(), resolved[i].rendered);
        }
    }

    return result;
}
```

Null byte prefix (`\x00`) ensures placeholders never collide with real template content or
`@xxx{...}` syntax.

---

## Commit 3: World Isolation + Input Validation (`fix: world-isolation-and-input-validation`)

**Fixes:** 9 IMPORTANT backend issues
**Files:** `libs/http/src/worldbuilding_http_handler.cpp`, `libs/worldbuilding/src/worldbuilding_tools.cpp`,
`libs/storage/src/session_store.cpp`, `libs/skills/src/skill_loader.cpp`,
`libs/skills/include/merak/skills/skill_loader.hpp`, `libs/skills/src/skill_registry.cpp`

### 3a. HTTP PATCH handlers — world isolation + existence check

**Files:** `libs/http/src/worldbuilding_http_handler.cpp` (lines 842-894)

**Problem:** `handle_patch_scene`, `handle_patch_chapter`, `handle_patch_foreshadow`,
`handle_patch_secret` do not verify that the resource belongs to the specified world.
They always return `{"ok": true}` even when the resource doesn't exist.

**Fix (scene example, others follow same pattern):**
```cpp
void WorldbuildingHttpHandler::handle_patch_scene(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    std::string sid = req.matches[2];
    try {
        // World isolation: get_scene takes world_id, returns nullopt if not in this world
        auto scene = service_->narrative().get_scene(wid, sid);
        if (!scene) {
            error_response(res, "Scene not found", 404, "scene_not_found");
            return;
        }
        auto body = nlohmann::json::parse(req.body);
        auto fields = body.at("fields");
        // Validate status if present
        if (fields.contains("status")) {
            std::string s = fields["status"].get<std::string>();
            if (!is_valid_scene_status(s)) {
                error_response(res, "Invalid scene status: " + s, 400);
                return;
            }
        }
        service_->narrative().patch_scene(wid, sid, fields);
        json_response(res, {{"ok", true}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}
```

For `handle_patch_foreshadow` and `handle_patch_secret`, additionally check the bool return
value of the `patch` method since these stores return false when the resource doesn't exist.

### 3b. Agent Tools — world isolation

**Files:** `libs/worldbuilding/src/worldbuilding_tools.cpp`

**Problem:** `UpdateCharacterCardTool`, `AddCharacterDiaryTool`, `AddRelationTool` load
agents without verifying they belong to `ctx_.world_id`. An LLM agent in world A can
operate on agents in world B.

**Fix — `UpdateCharacterCardTool` (line 1734):**
```cpp
auto agent_opt = svc.agents().get_agent(agent_id);
if (!agent_opt) {
    // existing not-found handling
    ...
}
// NEW: world isolation check
if (agent_opt->world_id != ctx_.world_id) {
    result.output = error_response(ToolErrorCode::NOT_FOUND,
        "角色 '" + agent_id + "' 不在当前世界中。");
    return result;
}
```

Same pattern for `AddCharacterDiaryTool` (line 1803) and `AddRelationTool` (lines 1900, 1906,
verify both source and target agents).

`UpdateForeshadowTool` (line 1974): already uses `ctx_.world_id` and the store layer
constructs paths from `world_id`, providing implicit isolation. No change needed.

### 3c. `handle_list_agents` — world existence check

**File:** `libs/http/src/worldbuilding_http_handler.cpp` (line 318)

**Problem:** Returns empty list (HTTP 200) for non-existent worlds instead of 404.

**Fix:** Add `service_->worlds().get_world(wid)` check at handler start, return 404 if missing.

### 3d. PATCH handlers — VersionConflictError handling

**File:** `libs/http/src/worldbuilding_http_handler.cpp`

**Problem:** Only `handle_patch_agent` catches `VersionConflictError` specifically and returns 409.
The four other PATCH handlers catch only `std::exception` and return generic 400.

**Fix:** Add `catch (const worldbuilding::VersionConflictError& e)` before the generic catch in
each PATCH handler, matching the pattern from `handle_patch_agent` (lines 730-740).

### 3e. `read_foreshadowing` — remove Paid status rejection

**File:** `libs/worldbuilding/src/worldbuilding_tools.cpp` (lines 630-634)

**Problem:** Tool rejects reading foreshadowing items with Paid status. The error message
is misleading ("不可再次偿还" - cannot pay again) when the user is trying to read, not pay.
This is inconsistent with `read_secret` which only rejects Abandoned.

**Fix:** Delete the Paid status gate (lines 630-634). Users should be able to read paid
foreshadowing details.

### 3f. `SkillLoader` — return error info on malformed frontmatter

**Files:** `libs/skills/include/merak/skills/skill_loader.hpp`, `libs/skills/src/skill_loader.cpp`,
`libs/skills/src/skill_registry.cpp`

**Problem:** When YAML frontmatter is malformed, `SkillLoader::load` returns `std::nullopt`
with no indication of what went wrong (which line, what error).

**Fix:** Change return type from `std::optional<SkillDef>` to `std::expected<SkillDef, std::string>`.
Each `return std::nullopt` becomes `return std::unexpected("line N: <reason>")`.

**Header change:**
```cpp
#include <expected>
static std::expected<SkillDef, std::string> load(const std::filesystem::path& path);
```

**Call site update (skill_registry.cpp:13):**
```cpp
auto result = SkillLoader::load(entry.path());
if (result) {
    if (skills_.find(result->name) == skills_.end()) {
        skills_[result->name] = std::move(*result);
    }
} else {
    // Log or surface the error
    MERAK_LOG_WARN("Failed to load skill {}: {}", entry.path().string(), result.error());
}
```

### 3g. `events_after` — don't break on parse error

**File:** `libs/storage/src/session_store.cpp` (line 239)

**Problem:** `catch(...) { break; }` silently discards all events after a corrupt journal line.
One bad line in the middle of a journal file causes data loss for replay.

**Fix:** Change `break` to `continue` — skip the corrupt line and continue processing
subsequent valid lines.

### 3h. PATCH handlers — check return value

**Files:** `libs/http/src/worldbuilding_http_handler.cpp` (lines 876, 889)

**Problem:** `handle_patch_foreshadow` and `handle_patch_secret` call `patch()` which returns
`bool` (false when resource not found), but the return value is discarded. Always returns
`{"ok": true}`.

**Fix:** Check the return value; return 404 when false:
```cpp
bool ok = service_->foreshadowing().patch(wid, fid, fields);
if (!ok) {
    error_response(res, "Foreshadow not found", 404, "foreshadow_not_found");
    return;
}
json_response(res, {{"ok", true}});
```

---

## Commit 4: WebUI State + Error Handling (`fix: webui-state-and-error-handling`)

**Fixes:** 13 IMPORTANT frontend issues
**Files:** `webui/src/components/Inspector/AgentCardView.tsx`, `webui/src/components/ChapterEditor.tsx`,
`webui/src/components/Inspector/CreationDashboard.tsx`, `webui/src/components/Inspector/AgentCardEdit.tsx`,
`webui/src/components/Sidebar/SkillBrowser.tsx`, `webui/src/components/Sidebar/PipelineNavigator.tsx`,
`webui/src/AppState.tsx`

### 4a. `AgentCardView.tsx` — race condition + null assertion (2 fixes)

**Race condition fix:** Add `AbortController` + cleanup in `useEffect`. Wrap `load` in `useCallback`.
```tsx
const abortRef = useRef<AbortController>();

const load = useCallback(async () => {
  if (!state.worldId) return;
  abortRef.current?.abort();
  const controller = new AbortController();
  abortRef.current = controller;
  setLoading(true);
  setError(null);
  try {
    const res = await api.fetchAgentDetail(state.worldId, agentId);
    if (!controller.signal.aborted) { setDetail(res.agent); setLoading(false); }
  } catch (e) {
    if (!controller.signal.aborted) { setError((e as Error).message); setLoading(false); }
  }
}, [agentId, state.worldId]);

useEffect(() => { load(); return () => abortRef.current?.abort(); }, [load]);
```

**Null assertion fix:** Add explicit null guard for `state.worldId` before rendering `AgentCardEdit`.

### 4b. `ChapterEditor.tsx` — stale state + content loading + word count (3 fixes)

**State reset:** Add `useEffect` on `chapterId` that resets `title` and `content` and fetches chapter body from API.
**Word count:** Detect CJK characters vs word-separated text. For CJK-dominant content, display character count.
```
Display: "1,234 字" (for Chinese) or "567 words" (for English)
```

### 4c. `CreationDashboard.tsx` — dead code + real relation graph (2 fixes)

**Dead code:** Remove the unused `relationData` `useMemo` (lines 33-51).

**Relation graph:** Load real relations via existing `fetchRelations` API, aggregate into graph data, pass to `RelationGraph`.
```tsx
const [relations, setRelations] = useState<RelationEntry[]>([]);

useEffect(() => {
  if (!state.worldId || !state.agents.length) return;
  Promise.all(state.agents.map(a =>
    api.fetchRelations(state.worldId!, a.agent_id)
  )).then(results => {
    setRelations(results.flatMap(r => r.relations));
  }).catch(e => { /* surface error */ });
}, [state.worldId, state.agents]);

const graphData = useMemo(() => {
  const nodes = state.agents.map(a => ({ id: a.agent_id, label: a.display_name || a.name }));
  const links = relations.map(r => ({
    source: r.agent_id, target: r.target_id, kind: r.relation_type
  }));
  return { nodes, links };
}, [state.agents, relations]);
```

### 4d. `AgentCardEdit.tsx` — version conflict + taboo_topics (2 fixes)

**Version conflict:** Replace `e.message?.includes('version')` substring match with
check for HTTP 409 status code and error code `"version_conflict"` from structured API response.

**Taboo topics:** Add form field for `taboo_topics` (tag-style input, comma or enter to add).

### 4e. `SkillBrowser.tsx` — error feedback (1 fix)

**Fix:** Add error state and inline error message in catch block instead of silent ignore.

### 4f. `PipelineNavigator.tsx` — type safety (1 fix)

**Fix:** Runtime validation of `pipelinePhase` against known `CreativePhase` values.
Fallback to `'worldbuilding'` for unrecognized values.

### 4g. `AppState.tsx` — 3 fixes

**`card_updated`:** Don't set `worldbuildingStatus: 'loading'` (card update doesn't invalidate worldbuilding).
Alternatively, emit a corresponding `worldbuilding_ready` event from server.

**`p.phase` cast:** Add `typeof p.phase === 'string'` runtime guard before assignment.

**`pipeline_stats_updated`:** Add `// TODO: wire pipeline stats into state` comment.

---

## Verification

After each commit, verify:
1. `cmake --build build` passes with no errors
2. `cd webui && npx tsc --noEmit` passes
3. Existing tests pass
4. For Commit 1: manually test `update_character_card` tool with existing card (was broken, should now work)
5. For Commit 2: render a template where one reference expands to contain another `@xxx{...}` pattern
6. For Commit 3: attempt to PATCH a resource from a different world — must return 404
7. For Commit 4: navigate between chapters, verify title/content reset; invoke skill and force network error
