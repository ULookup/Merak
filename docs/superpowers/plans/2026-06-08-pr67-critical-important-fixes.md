# PR #67 CRITICAL + IMPORTANT Fixes — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix 23 CRITICAL + IMPORTANT issues found in PR #67 code review, organized into 4 independent commits.

**Architecture:** Four self-contained commits by module: (1) version conflict + atomic write in worldbuilding store, (2) DSL renderer two-pass placeholder substitution, (3) world isolation + input validation across HTTP handlers and tools, (4) webui state management, error handling, and type safety.

**Tech Stack:** C++23, React 18 + TypeScript, libpq, nlohmann/json

---

## File Structure

```
libs/worldbuilding/src/agent_store.cpp          — Commit 1: version check + reorder write ops
libs/worldbuilding/src/worldbuilding_tools.cpp   — Commit 1+3: call_id, version catch, world isolation
libs/context_dsl/src/renderer.cpp                — Commit 2: two-pass placeholder renderer
libs/http/src/worldbuilding_http_handler.cpp     — Commit 3: PATCH isolation, status validation, 404, 409
libs/storage/src/session_store.cpp               — Commit 3: events_after continue-on-error
libs/skills/include/merak/skills/skill_loader.hpp — Commit 3: std::expected return type
libs/skills/src/skill_loader.cpp                 — Commit 3: error detail on nullopt paths
libs/skills/src/skill_registry.cpp               — Commit 3: handle std::expected result
webui/src/api/client.ts                          — Commit 4: ApiError class with status + code
webui/src/components/Inspector/AgentCardView.tsx — Commit 4: AbortController + null guard
webui/src/components/ChapterEditor.tsx           — Commit 4: state reset, content load, CJK word count
webui/src/components/Inspector/CreationDashboard.tsx — Commit 4: dead code removal, real relations
webui/src/components/Inspector/AgentCardEdit.tsx — Commit 4: ApiError check, taboo_topics field
webui/src/components/Sidebar/SkillBrowser.tsx    — Commit 4: error state feedback
webui/src/components/Sidebar/PipelineNavigator.tsx — Commit 4: runtime phase validation
webui/src/AppState.tsx                           — Commit 4: card_updated, p.phase, pipeline_stats
```

---

## Commit 1: Version Conflict Fix + Atomic Write

### Task 1.1: Fix patch_character_card — skip version check when version=0, reorder to DB-first

**Files:**
- Modify: `libs/worldbuilding/src/agent_store.cpp:542-616`

- [ ] **Step 1: Apply the three changes to patch_character_card**

Replace the method body starting at line 542 with the reordered version:

```cpp
CharacterCard
AgentStore::patch_character_card(const std::string& agent_id,
                                  const nlohmann::json& fields,
                                  int expected_version) {
    auto record = get_agent(agent_id);
    if (!record.has_value() || record->kind != AgentKind::Individual) {
        throw std::runtime_error("agent is not a character: " + agent_id);
    }

    auto current = load_character_card(agent_id);
    // version=0 means no version check (force overwrite)
    if (expected_version > 0 && current.version != expected_version) {
        throw VersionConflictError(current.version);
    }

    // 仅更新传入的字段
    if (fields.contains("core_traits")) {
        current.core_traits.clear();
        for (const auto& t : fields["core_traits"]) {
            current.core_traits.push_back(t.get<std::string>());
        }
    }
    if (fields.contains("background")) current.background = fields["background"].get<std::string>();
    if (fields.contains("emotional_tendency")) current.emotional_tendency = fields["emotional_tendency"].get<std::string>();
    if (fields.contains("speaking_style")) current.speaking_style = fields["speaking_style"].get<std::string>();
    if (fields.contains("core_desire")) current.core_desire = fields["core_desire"].get<std::string>();
    if (fields.contains("deep_fear")) current.deep_fear = fields["deep_fear"].get<std::string>();
    if (fields.contains("daily_goal")) current.daily_goal = fields["daily_goal"].get<std::string>();
    if (fields.contains("knowledge_scope")) current.knowledge_scope = fields["knowledge_scope"].get<std::string>();
    if (fields.contains("appearance")) current.appearance = fields["appearance"].get<std::string>();
    if (fields.contains("age")) current.age = fields["age"].get<int>();
    if (fields.contains("gender")) current.gender = fields["gender"].get<std::string>();
    if (fields.contains("race")) current.race = fields["race"].get<std::string>();
    if (fields.contains("identity")) current.identity = fields["identity"].get<std::string>();
    if (fields.contains("taboo_topics")) {
        current.taboo_topics.clear();
        for (const auto& t : fields["taboo_topics"]) {
            current.taboo_topics.push_back(t.get<std::string>());
        }
    }

    current.agent_id = agent_id;
    current.version = (expected_version > 0) ? expected_version + 1 : current.version + 1;
    current.updated_at = now_iso_utc();
    const auto markdown = character_card_markdown(current);
    const auto root = agent_path(agent_id);

    // Write DB FIRST with version guard. Only write files after DB confirms success.
    // This prevents the TOCTOU window where file is written but DB update fails.
    PgConn conn(*pool_);

    conn.execute(
        "UPDATE agents SET name = $1, display_name = $2, updated_at = $3 "
        "WHERE id = $4",
        {current.name, current.name, current.updated_at, agent_id});

    int affected = conn.execute(
        "UPDATE character_cards SET name = $1, age = $2, gender = $3, race = $4, "
        "identity = $5, core_traits = $6, emotional_tendency = $7, "
        "speaking_style = $8, taboo_topics = $9, core_desire = $10, "
        "deep_fear = $11, daily_goal = $12, background = $13, "
        "knowledge_scope = $14, appearance = $15, version = $16, updated_at = $17 "
        "WHERE agent_id = $18 AND version = $19",
        {current.name, std::to_string(current.age),
         current.gender, current.race, current.identity,
         to_pg_array(current.core_traits), current.emotional_tendency,
         current.speaking_style, to_pg_array(current.taboo_topics),
         current.core_desire, current.deep_fear, current.daily_goal,
         current.background, current.knowledge_scope, current.appearance,
         std::to_string(current.version), current.updated_at, agent_id,
         std::to_string(expected_version > 0 ? expected_version : current.version - 1)});

    if (expected_version > 0 && affected == 0) {
        throw VersionConflictError(expected_version);
    }

    // DB confirmed — now write files
    write_text(root / "character_card.md", markdown);
    write_text(root / "character_card_history" /
                   history_filename(current.updated_at, current.version),
               markdown + "\n更新原因：patch_user\n");

    return current;
}
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build build
```

Expected: compilation passes with no errors.

- [ ] **Step 3: Commit**

```bash
git add libs/worldbuilding/src/agent_store.cpp
git commit -m "$(cat <<'EOF'
fix: skip version check when version=0 and make patch_character_card atomic

- expected_version=0 now means "skip version check" (matches original intent)
- Reorder: DB update first with version guard in WHERE clause, files second
- DB-level conflict detection via rows_affected() check
- Version increment adapts to version=0 case
EOF
)"
```

### Task 1.2: Fix UpdateCharacterCardTool — catch VersionConflictError, add world isolation check

**Files:**
- Modify: `libs/worldbuilding/src/worldbuilding_tools.cpp:1712-1755`

- [ ] **Step 1: Add VersionConflictError catch and world isolation check**

Replace the `UpdateCharacterCardTool::execute` lambda body (lines 1712-1755):

```cpp
std::future<ToolResult> UpdateCharacterCardTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = json::parse(call.arguments);
            std::string agent_id = args.value("agent_id", "");

            if (agent_id.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "更新角色卡失败。缺少必填字段：agent_id。");
                return result;
            }
            if (!args.contains("fields") || !args["fields"].is_object()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "更新角色卡失败。缺少必填字段：fields，且必须为对象。");
                return result;
            }

            auto& svc = *static_cast<UpdateCharacterCardTool&>(*self).svc_;
            auto& ctx = static_cast<UpdateCharacterCardTool&>(*self).ctx_;

            auto agent_opt = svc.agents().get_agent(agent_id);
            if (!agent_opt) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "角色 '" + agent_id + "' 不存在。");
                return result;
            }
            if (agent_opt->world_id != ctx.world_id) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "角色 '" + agent_id + "' 不在当前世界中。");
                return result;
            }

            // version=0 means no version check
            auto updated = svc.agents().patch_character_card(agent_id, args["fields"], 0);

            result.output = ok_response({
                {"agent_id", updated.agent_id},
                {"version", updated.version}
            });

        } catch (const worldbuilding::VersionConflictError& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::VERSION_CONFLICT,
                "卡片已被其他来源修改（当前版本：" + std::to_string(e.current_version) + "），请刷新后重试");
        } catch (const std::exception& e) {
            result.is_error = true;
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("update_character_card 内部错误: ") + e.what());
        }
        return result;
    });
}
```

Note: `ctx_` is accessed via `static_cast<UpdateCharacterCardTool&>(*self).ctx_`. If `UpdateCharacterCardTool` does not already have a `ctx_` member, check the class definition — it likely inherits from a base that stores `ToolExecutionContext`. If not, add `const ToolExecutionContext& ctx_;` as a member and initialize it from the `execute` parameter.

- [ ] **Step 2: Build and verify**

```bash
cmake --build build
```

- [ ] **Step 3: Commit**

```bash
git add libs/worldbuilding/src/worldbuilding_tools.cpp
git commit -m "$(cat <<'EOF'
fix: catch VersionConflictError in UpdateCharacterCardTool with structured error

- Add specific catch for VersionConflictError before generic handler
- Add world isolation check (agent must belong to current world)
- Returns structured error with current version info
EOF
)"
```

### Task 1.3: Fix UpdateAgentPromptTool — set call_id

**Files:**
- Modify: `libs/worldbuilding/src/worldbuilding_tools.cpp:1661-1691`

- [ ] **Step 1: Add call_id to both success and error ToolResult objects**

In `UpdateAgentPromptTool::execute`, find the two `ToolResult r;` declarations (lines 1671 and 1679 for success paths, line 1686 for error path) and add `r.call_id = call.id;` to each.

The success path at line 1671:
```cpp
            if (agent_id.empty() || prompt.empty()) {
                ToolResult r;
                r.call_id = call.id;   // ADD THIS
                r.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "agent_id 和 prompt 都不能为空");
                return r;
            }
```

The success path at line 1679:
```cpp
            ToolResult r;
            r.call_id = call.id;       // ADD THIS
            r.output = ok_response({
                {"agent_id", agent_id},
                {"message", "系统提示词已更新"}
            });
            return r;
```

The error catch at line 1685:
```cpp
        } catch (const std::exception& e) {
            ToolResult r;
            r.call_id = call.id;       // ADD THIS
            r.output = error_response(ToolErrorCode::INTERNAL, e.what());
            return r;
        }
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build build
```

- [ ] **Step 3: Commit**

```bash
git add libs/worldbuilding/src/worldbuilding_tools.cpp
git commit -m "$(cat <<'EOF'
fix: set call_id in UpdateAgentPromptTool result for downstream correlation

Both success and error paths now include call_id in ToolResult.
EOF
)"
```

> **Note:** If Tasks 1.2 and 1.3 modify the same file, they can be squashed into a single commit.

---

## Commit 2: DSL Renderer Cascading Replacement Fix

### Task 2.1: Rewrite Renderer::render with two-pass placeholder substitution

**Files:**
- Modify: `libs/context_dsl/src/renderer.cpp`

- [ ] **Step 1: Replace the render method**

Replace the entire file content:

```cpp
#include <merak/dsl/renderer.hpp>

#include <string>
#include <vector>

namespace merak::dsl {

std::string Renderer::render(const std::string& template_text,
                              const std::vector<ResolvedContent>& resolved) {
    std::string result = template_text;

    // Pass 1: replace each @xxx{...} reference with a unique null-byte-delimited placeholder.
    // This ensures no rendered content can accidentally match another reference pattern.
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

    // Pass 2: replace each placeholder with the actual rendered content.
    // Since placeholders use \x00 (never present in normal text), no collision possible.
    for (size_t i = 0; i < resolved.size(); ++i) {
        auto pos = result.find(placeholders[i]);
        if (pos != std::string::npos) {
            result.replace(pos, placeholders[i].length(), resolved[i].rendered);
        }
    }

    return result;
}

} // namespace merak::dsl
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build build
```

- [ ] **Step 3: Commit**

```bash
git add libs/context_dsl/src/renderer.cpp
git commit -m "$(cat <<'EOF'
fix: use two-pass placeholder substitution in DSL renderer

Prevents cascading replacement where rendered content of one reference
accidentally matches the raw pattern of another. Pass 1 replaces all
@xxx{...} with null-byte-delimited placeholders. Pass 2 substitutes
placeholders with final rendered content.
EOF
)"
```

---

## Commit 3: World Isolation + Input Validation

### Task 3.1: Fix HTTP PATCH handlers — world isolation, existence check, status validation, VersionConflictError

**Files:**
- Modify: `libs/http/src/worldbuilding_http_handler.cpp:842-894`

- [ ] **Step 1: Replace handle_patch_scene**

Replace the handler at line 842:

```cpp
void WorldbuildingHttpHandler::handle_patch_scene(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    std::string sid = req.matches[2];
    try {
        auto scene = service_->narrative().get_scene(wid, sid);
        if (!scene) {
            error_response(res, "Scene not found", 404, "scene_not_found");
            return;
        }
        auto body = nlohmann::json::parse(req.body);
        auto fields = body.at("fields");
        if (fields.contains("status")) {
            std::string s = fields["status"].get<std::string>();
            if (s != "drafting" && s != "writing" && s != "completed" && s != "archived") {
                error_response(res, "Invalid scene status: " + s, 400);
                return;
            }
        }
        service_->narrative().patch_scene(wid, sid, fields);
        json_response(res, {{"ok", true}});
    } catch (const worldbuilding::VersionConflictError& e) {
        nlohmann::json j = {
            {"ok", false},
            {"error", {
                {"code", "version_conflict"},
                {"message", "资源已被其他来源修改，请刷新后重试"},
                {"current_version", e.current_version},
                {"retryable", true}
            }}
        };
        res.status = 409;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}
```

- [ ] **Step 2: Replace handle_patch_chapter**

Replace the handler at line 855 with the same pattern, using `get_chapter` (if available) or the store's `patch_chapter` return value for existence check. Status validation: `"drafting" | "writing" | "completed" | "archived"`.

- [ ] **Step 3: Replace handle_patch_foreshadow**

Replace the handler at line 870:

```cpp
void WorldbuildingHttpHandler::handle_patch_foreshadow(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    std::string fid = req.matches[2];
    try {
        auto body = nlohmann::json::parse(req.body);
        auto fields = body.at("fields");
        if (fields.contains("status")) {
            std::string s = fields["status"].get<std::string>();
            if (s != "open" && s != "paid" && s != "abandoned") {
                error_response(res, "Invalid foreshadow status: " + s, 400);
                return;
            }
        }
        bool ok = service_->foreshadowing().patch(wid, fid, fields);
        if (!ok) {
            error_response(res, "Foreshadow not found", 404, "foreshadow_not_found");
            return;
        }
        json_response(res, {{"ok", true}});
    } catch (const worldbuilding::VersionConflictError& e) {
        nlohmann::json j = {
            {"ok", false},
            {"error", {
                {"code", "version_conflict"},
                {"message", "资源已被其他来源修改，请刷新后重试"},
                {"current_version", e.current_version},
                {"retryable", true}
            }}
        };
        res.status = 409;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}
```

- [ ] **Step 4: Replace handle_patch_secret**

Replace the handler at line 883, same pattern as handle_patch_foreshadow but with `service_->secrets().patch(wid, sid, fields)`. Status validation: `"active" | "revealed" | "abandoned"`.

- [ ] **Step 5: Build and verify**

```bash
cmake --build build
```

### Task 3.2: Add world isolation to AddCharacterDiaryTool and AddRelationTool

**Files:**
- Modify: `libs/worldbuilding/src/worldbuilding_tools.cpp:1779-1847` (AddCharacterDiaryTool)
- Modify: `libs/worldbuilding/src/worldbuilding_tools.cpp:1871-1934` (AddRelationTool)

- [ ] **Step 1: Add world isolation check to AddCharacterDiaryTool**

After line 1807 (`return result;` inside the `if (!agent_opt)` block), before line 1810, add:

```cpp
            if (agent_opt->world_id != ctx.world_id) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "角色 '" + agent_id + "' 不在当前世界中。");
                return result;
            }
```

Note: `AddCharacterDiaryTool` already accesses `ctx` at line 1801 (`auto& ctx = static_cast<AddCharacterDiaryTool&>(*self).ctx_;`).

- [ ] **Step 2: Add world isolation check to AddRelationTool**

After loading `src_opt` (line 1900) and `tgt_opt` (line 1906), add world checks for both. First add `auto& ctx = static_cast<AddRelationTool&>(*self).ctx_;` if not already present, then:

```cpp
            if (src_opt->world_id != ctx.world_id) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "角色 '" + source_id + "' 不在当前世界中。");
                return result;
            }
            if (tgt_opt->world_id != ctx.world_id) {
                result.output = error_response(ToolErrorCode::NOT_FOUND,
                    "角色 '" + target_id + "' 不在当前世界中。");
                return result;
            }
```

- [ ] **Step 3: Build and verify**

```bash
cmake --build build
```

### Task 3.3: Fix handle_list_agents — check world existence

**Files:**
- Modify: `libs/http/src/worldbuilding_http_handler.cpp:318-335`

- [ ] **Step 1: Add world existence check**

At the start of the try block in `handle_list_agents`, add:

```cpp
void WorldbuildingHttpHandler::handle_list_agents(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto world = service_->worlds().get_world(wid);
        if (!world) {
            error_response(res, "World not found", 404, "world_not_found");
            return;
        }
        auto agents = service_->agents().list_agents(wid);
        // ... rest unchanged
```

### Task 3.4: Fix read_foreshadowing — remove Paid status rejection

**Files:**
- Modify: `libs/worldbuilding/src/worldbuilding_tools.cpp:630-634`

- [ ] **Step 1: Delete the Paid status gate**

Remove lines 630-634 (the `if (f_opt->status == ForeshadowStatus::Paid)` block and its error response).

### Task 3.5: Fix SkillLoader — std::expected return type with error detail

**Files:**
- Modify: `libs/skills/include/merak/skills/skill_loader.hpp`
- Modify: `libs/skills/src/skill_loader.cpp`
- Modify: `libs/skills/src/skill_registry.cpp`

- [ ] **Step 1: Update header**

Change `skill_loader.hpp` line 2 from `#include <optional>` to `#include <expected>`, and line 21:
```cpp
    static std::expected<SkillDef, std::string> load(const std::filesystem::path& path);
```

- [ ] **Step 2: Update implementation return types**

In `skill_loader.cpp`, change the function signature and replace each `return std::nullopt` with `return std::unexpected("line N: <reason>")`. Example for the malformed line case (line 76):
```cpp
            if (colon_pos == std::string::npos) {
                return std::unexpected(
                    "line " + std::to_string(line_number) + ": missing colon in frontmatter key-value pair");
            }
```

- [ ] **Step 3: Update call site**

In `skill_registry.cpp`, change the `discover_from` loop (lines 13-19):
```cpp
            auto result = SkillLoader::load(entry.path());
            if (result) {
                if (skills_.find(result->name) == skills_.end()) {
                    skills_[result->name] = std::move(*result);
                }
            }
            // Errors are non-fatal — malformed skills are skipped
```

Add `#include <iostream>` or use the project's logging macro to log errors if desired.

### Task 3.6: Fix events_after — continue on parse error, don't break

**Files:**
- Modify: `libs/storage/src/session_store.cpp:239`

- [ ] **Step 1: Change break to continue**

Line 239, change `catch(...) { break; }` to `catch(...) { continue; }`.

### Task 3.7: Build, verify, and commit all Commit 3 changes

- [ ] **Step 1: Build**

```bash
cmake --build build
```

- [ ] **Step 2: Commit**

```bash
git add libs/http/src/worldbuilding_http_handler.cpp \
        libs/worldbuilding/src/worldbuilding_tools.cpp \
        libs/storage/src/session_store.cpp \
        libs/skills/include/merak/skills/skill_loader.hpp \
        libs/skills/src/skill_loader.cpp \
        libs/skills/src/skill_registry.cpp
git commit -m "$(cat <<'EOF'
fix: add world isolation, input validation, and error handling to backend

- World isolation checks on 4 HTTP PATCH handlers + 3 Agent Tools
- Status enum validation on all PATCH endpoints (reject invalid values)
- Proper 404 responses when resource not found or world mismatch
- VersionConflictError caught with 409 in all PATCH handlers
- handle_list_agents verifies world existence before listing
- read_foreshadowing no longer rejects Paid items
- SkillLoader returns std::expected with error detail on malformed frontmatter
- events_after skips corrupt journal lines instead of discarding all subsequent events
EOF
)"
```

---

## Commit 4: WebUI State + Error Handling

### Task 4.1: Add ApiError class to client.ts for structured error detection

**Files:**
- Modify: `webui/src/api/client.ts`

- [ ] **Step 1: Add ApiError class and update request function**

Add before the `request` function (after line 34):

```ts
export class ApiError extends Error {
  status: number;
  code?: string;
  constructor(message: string, status: number, code?: string) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
    this.code = code;
  }
}
```

Update the error-throwing block in `request` (lines 54-61) to:

```ts
  if (res.status >= 400) {
    const error = (json as { error?: { message?: string; code?: string } | string; message?: string }).error;
    const message =
      typeof error === 'string'
        ? error
        : (error?.message ?? (json as { message?: string }).message ?? `HTTP ${res.status}`);
    const code = (typeof error === 'object' && error !== null) ? error.code : undefined;
    throw new ApiError(message, res.status, code);
  }
```

- [ ] **Step 2: TypeScript check**

```bash
cd webui && npx tsc --noEmit
```

### Task 4.2: Fix AgentCardView — AbortController + null guard

**Files:**
- Modify: `webui/src/components/Inspector/AgentCardView.tsx`

- [ ] **Step 1: Replace the load function and useEffect**

Replace lines 1-29:

```tsx
import { useState, useEffect, useCallback, useRef } from 'react';
import { api } from '../../api/client';
import type { AgentDetail } from '../../api/types';
import { useAppState } from '../../AppState';
import AgentCardEdit from './AgentCardEdit';
import styles from './AgentCardView.module.css';

interface Props {
  agentId: string;
  onClose: () => void;
}

export default function AgentCardView({ agentId, onClose }: Props) {
  const { state } = useAppState();
  const [detail, setDetail] = useState<AgentDetail | null>(null);
  const [editMode, setEditMode] = useState(false);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
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
      if (!controller.signal.aborted) {
        setDetail(res.agent);
        setLoading(false);
      }
    } catch (e) {
      if (!controller.signal.aborted) {
        setError((e as Error).message);
        setLoading(false);
      }
    }
  }, [agentId, state.worldId]);

  useEffect(() => {
    load();
    return () => abortRef.current?.abort();
  }, [load]);
```

- [ ] **Step 2: Replace the null assertion on line 38**

Change:
```tsx
        worldId={state.worldId!}
```
To:
```tsx
        worldId={state.worldId ?? ''}
```
And in `AgentCardEdit`, if `worldId` is empty string, show an error. Or better, add a guard before rendering `AgentCardEdit`:

```tsx
  if (editMode) {
    if (!state.worldId) return <div className={styles.container}>No world selected</div>;
    return (
      <AgentCardEdit
        worldId={state.worldId}
        ...
```

- [ ] **Step 3: TypeScript check**

```bash
cd webui && npx tsc --noEmit
```

### Task 4.3: Fix ChapterEditor — stale state, content loading, CJK word count

**Files:**
- Modify: `webui/src/components/ChapterEditor.tsx`

- [ ] **Step 1: Add chapterId dependency for state reset and content loading**

Add after line 42 (the entity-building useEffect):

```tsx
  // Reset title and content when chapterId changes, and load chapter content
  useEffect(() => {
    setTitle(chapter?.title ?? '');
    setContent('');
    if (!chapterId || !worldId) return;
    let cancelled = false;
    api.fetchChapterContent(worldId, chapterId)
      .then(data => {
        if (!cancelled) setContent(data.content ?? '');
      })
      .catch(() => {
        // Content not yet saved or fetch failed — start with empty editor
      });
    return () => { cancelled = true; };
  }, [chapterId, worldId]); // deliberately NOT depending on chapter — we reset on chapterId change
```

Note: If `fetchChapterContent` doesn't exist in the API client, add it:

In `webui/src/api/client.ts`, in the `api` export object:
```ts
  fetchChapterContent: (worldId: string, chapterId: string) =>
    request<{ ok: boolean; content?: string }>('GET', `/api/worldbuilding/${worldId}/chapters/${chapterId}`),
```

- [ ] **Step 2: Fix word count for CJK text**

Replace line 45:
```tsx
  const wordCount = content.trim() ? content.trim().split(/\s+/).length : 0;
  const charCount = content.length;
```
With:
```tsx
  const trimmed = content.trim();
  const cjkChars = (trimmed.match(/[一-鿿㐀-䶿　-〿＀-￯]/g) || []).length;
  const isCJK = trimmed.length > 0 && cjkChars / trimmed.length > 0.3;
  const wordCount = trimmed
    ? (isCJK ? cjkChars : trimmed.split(/\s+/).length)
    : 0;
  const charCount = content.length;
```

And update line 91 from `{wordCount} 词 | {charCount} 字` to:
```tsx
            {isCJK ? `${wordCount} 字` : `${wordCount} words`} | {charCount} 字符
```

- [ ] **Step 3: Add location CSS class for entity tags**

In `webui/src/components/ChapterEditor.module.css`, add:
```css
.location {
  background: rgba(129, 199, 132, 0.15);
  border-color: #81c784;
}
```

- [ ] **Step 4: TypeScript check**

```bash
cd webui && npx tsc --noEmit
```

### Task 4.4: Fix CreationDashboard — remove dead code, wire real relations

**Files:**
- Modify: `webui/src/components/Inspector/CreationDashboard.tsx`

- [ ] **Step 1: Remove dead relationData useMemo, add relations state and fetch**

Replace lines 1-51 (imports through the dead relationData useMemo):

```tsx
import { useState, useMemo, useEffect } from 'react';
import { useAppState } from '../../AppState';
import { api } from '../../api/client';
import type { ForeshadowingItem, SecretItem, RelationEntry } from '../../api/types';
import styles from './CreationDashboard.module.css';

type DashTab = 'foreshadow' | 'secrets' | 'relations';

interface GraphLink {
  source: string;
  target: string;
  kind: string;
}

export default function CreationDashboard() {
  const { state } = useAppState();
  const [tab, setTab] = useState<DashTab>('foreshadow');
  const [relations, setRelations] = useState<RelationEntry[]>([]);
  const [relationsLoading, setRelationsLoading] = useState(false);

  // Group foreshadowing by status
  const foreshadowGroups = useMemo(() => {
    const open: ForeshadowingItem[] = [];
    const paid: ForeshadowingItem[] = [];
    const abandoned: ForeshadowingItem[] = [];
    state.foreshadowing.forEach(f => {
      const s = f.status ?? 'open';
      if (s === 'paid') paid.push(f);
      else if (s === 'abandoned') abandoned.push(f);
      else open.push(f);
    });
    return { open, paid, abandoned };
  }, [state.foreshadowing]);

  // Load real relations for all agents
  useEffect(() => {
    if (!state.worldId || !state.agents.length) {
      setRelations([]);
      return;
    }
    setRelationsLoading(true);
    Promise.all(state.agents.map(a =>
      api.fetchRelations(state.worldId!, a.id)
        .then(r => r.relations)
        .catch(() => [] as RelationEntry[])
    )).then(results => {
      setRelations(results.flat());
      setRelationsLoading(false);
    });
  }, [state.worldId, state.agents]);

  // Build graph data from real relations
  const graphData = useMemo(() => {
    const nodes = state.agents.map(a => ({
      id: a.id,
      label: (a.display_name || a.name).slice(0, 6),
    }));
    const seen = new Set<string>();
    const links: GraphLink[] = [];
    relations.forEach(r => {
      const key = [r.agent_id, r.target_id].sort().join('--');
      if (!seen.has(key)) {
        seen.add(key);
        links.push({ source: r.agent_id, target: r.target_id, kind: r.relation_type });
      }
    });
    return { nodes, links };
  }, [state.agents, relations]);
```

- [ ] **Step 2: Update RelationGraph to accept links data**

Change the relations tab JSX (around line 96-101):
```tsx
        {tab === 'relations' && (
          <div className={styles.relationView}>
            <RelationGraph
              agents={state.agents}
              links={graphData.links}
            />
```

Update the `RelationGraph` component signature and rendering:
```tsx
function RelationGraph({ agents, links }: {
  agents: { id: string; display_name: string; name: string }[];
  links: { source: string; target: string; kind: string }[];
}) {
  const n = agents.length;
  if (n === 0) return <p className={styles.empty}>暂无角色关系数据</p>;

  const cx = 140, cy = 120, r = 90;
  const nodeMap = new Map<string, { x: number; y: number }>();
  const nodes = agents.map((a, i) => {
    const angle = (2 * Math.PI * i) / n - Math.PI / 2;
    const pos = { x: cx + r * Math.cos(angle), y: cy + r * Math.sin(angle) };
    nodeMap.set(a.id, pos);
    return {
      id: a.id,
      label: (a.display_name || a.name).slice(0, 6),
      ...pos,
    };
  });

  // Build edge lines from real links
  const edgeLines = links
    .filter(l => nodeMap.has(l.source) && nodeMap.has(l.target))
    .map(l => {
      const s = nodeMap.get(l.source)!;
      const t = nodeMap.get(l.target)!;
      return { key: `${l.source}-${l.target}`, x1: s.x, y1: s.y, x2: t.x, y2: t.y };
    });

  return (
    <svg viewBox="0 0 280 240" className={styles.graph}>
      {edgeLines.map(e => (
        <line
          key={e.key}
          x1={e.x1} y1={e.y1}
          x2={e.x2} y2={e.y2}
          stroke="#4fc3f7"
          strokeWidth={1}
          strokeDasharray="4 2"
        />
      ))}
      {nodes.map(node => (
        <g key={node.id}>
          <circle cx={node.x} cy={node.y} r={16} fill="#2a2a4a" stroke="#4fc3f7" strokeWidth={1.5} />
          <text
            x={node.x}
            y={node.y + 4}
            textAnchor="middle"
            fill="#ddd"
            fontSize={9}
          >
            {node.label}
          </text>
        </g>
      ))}
    </svg>
  );
}
```

- [ ] **Step 3: Update color prop type**

Change line 121 from `color: string` to `color: 'open' | 'paid' | 'abandoned'`:

```tsx
function Column({ title, items, color }: { title: string; items: ForeshadowingItem[]; color: 'open' | 'paid' | 'abandoned' }) {
```

- [ ] **Step 4: TypeScript check**

```bash
cd webui && npx tsc --noEmit
```

### Task 4.5: Fix AgentCardEdit — ApiError-based version conflict + taboo_topics field

**Files:**
- Modify: `webui/src/components/Inspector/AgentCardEdit.tsx`

- [ ] **Step 1: Replace version conflict detection**

Change the import to include `ApiError`:
```tsx
import { api, ApiError } from '../../api/client';
```

Replace lines 55-58 (the catch block):
```tsx
    } catch (e: unknown) {
      if (e instanceof ApiError && e.code === 'version_conflict') {
        setConflict(true);
      }
    } finally {
```

- [ ] **Step 2: Add taboo_topics to fields state and form**

Add `taboo_topics` to the initial fields state (after line 29):
```tsx
    taboo_topics: cc.taboo_topics?.join('、') ?? '',
```

Add the form field before the actions div (after line 98):
```tsx
        {field('禁忌话题（逗号或空格分隔）', 'taboo_topics')}
```

- [ ] **Step 3: TypeScript check**

```bash
cd webui && npx tsc --noEmit
```

### Task 4.6: Fix SkillBrowser — surface errors to user

**Files:**
- Modify: `webui/src/components/Sidebar/SkillBrowser.tsx`

- [ ] **Step 1: Add error state and inline feedback**

Add state after line 44:
```tsx
  const [error, setError] = useState<string | null>(null);
```

Replace the handleInvoke catch block (lines 51-52):
```tsx
    } catch (e: unknown) {
      setError((e as Error).message || '调用失败');
      setTimeout(() => setError(null), 5000);
    } finally {
```

Add error display before the closing `</div>` (before line 96):
```tsx
      {error && (
        <p className={styles.error}>{error}</p>
      )}
```

Add the CSS class in `SkillBrowser.module.css`:
```css
.error {
  color: #ef5350;
  font-size: 0.8rem;
  padding: 6px 8px;
  margin-top: 8px;
  background: rgba(239, 83, 80, 0.1);
  border-radius: 4px;
}
```

- [ ] **Step 2: TypeScript check**

```bash
cd webui && npx tsc --noEmit
```

### Task 4.7: Fix PipelineNavigator — runtime phase validation

**Files:**
- Modify: `webui/src/components/Sidebar/PipelineNavigator.tsx`

- [ ] **Step 1: Add runtime validation**

Replace line 15:
```tsx
  const currentPhase: CreativePhase = (state.pipelinePhase ?? 'worldbuilding') as CreativePhase;
  const currentIdx = PHASES.findIndex(p => p.key === currentPhase);
```
With:
```tsx
  const VALID_PHASES = new Set<string>(['worldbuilding', 'character_creation', 'plot_architecture', 'scene_writing', 'reflection']);
  const rawPhase = state.pipelinePhase ?? 'worldbuilding';
  const currentPhase: CreativePhase = VALID_PHASES.has(rawPhase)
    ? (rawPhase as CreativePhase)
    : 'worldbuilding';
  const currentIdx = PHASES.findIndex(p => p.key === currentPhase);
```

- [ ] **Step 2: TypeScript check**

```bash
cd webui && npx tsc --noEmit
```

### Task 4.8: Fix AppState — card_updated, p.phase, pipeline_stats_updated

**Files:**
- Modify: `webui/src/AppState.tsx:695-709`

- [ ] **Step 1: Fix card_updated handler**

Replace lines 695-700:
```tsx
    case 'card_updated':
      return {
        ...state,
        storyVersion: state.storyVersion + 1,
      };
```

Remove `worldbuildingStatus: 'loading'` since card update doesn't invalidate worldbuilding data.

- [ ] **Step 2: Fix pipeline_phase_changed handler**

Replace lines 702-706:
```tsx
    case 'pipeline_phase_changed':
      return {
        ...state,
        pipelinePhase: typeof p.phase === 'string' ? p.phase : state.pipelinePhase,
      };
```

- [ ] **Step 3: Fix pipeline_stats_updated handler**

Replace lines 708-709:
```tsx
    case 'pipeline_stats_updated':
      // TODO: wire pipeline stats (e.g., word_count, scene_count) into AppState
      return state;
```

- [ ] **Step 4: TypeScript check**

```bash
cd webui && npx tsc --noEmit
```

### Task 4.9: Commit all Commit 4 changes

- [ ] **Step 1: Commit**

```bash
git add webui/src/api/client.ts \
        webui/src/components/Inspector/AgentCardView.tsx \
        webui/src/components/ChapterEditor.tsx \
        webui/src/components/Inspector/CreationDashboard.tsx \
        webui/src/components/Inspector/AgentCardEdit.tsx \
        webui/src/components/Sidebar/SkillBrowser.tsx \
        webui/src/components/Sidebar/SkillBrowser.module.css \
        webui/src/components/Sidebar/PipelineNavigator.tsx \
        webui/src/AppState.tsx
git commit -m "$(cat <<'EOF'
fix: resolve 13 IMPORTANT webui issues — state, errors, types, and data

- ApiError class with status/code for structured error detection
- AgentCardView: AbortController prevents race conditions, null guard for worldId
- ChapterEditor: resets on chapterId change, loads content from API, CJK-aware word count
- CreationDashboard: removes dead relationData useMemo, wires real relation graph
- AgentCardEdit: uses ApiError.code for version conflict, adds taboo_topics field
- SkillBrowser: surfaces invocation errors to user
- PipelineNavigator: runtime validation of pipelinePhase against known values
- AppState: card_updated no longer sets permanent loading, p.phase has typeof guard,
  pipeline_stats_updated documented as TODO stub
EOF
)"
```

---

## Final Verification

After all 4 commits:

```bash
# Backend
cmake --build build

# Frontend
cd webui && npx tsc --noEmit

# Run existing tests
cd build && ctest
```
