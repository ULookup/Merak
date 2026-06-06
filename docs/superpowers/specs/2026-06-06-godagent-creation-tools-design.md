# GodAgent Creation Tools Design

> **Status:** Approved
> **Date:** 2026-06-06
> **Goal:** Users can create all worldbuilding entities (scenes, chapters, arcs, secrets, world knowledge, locations) entirely through conversation with GodAgent, with per-creation confirmation via preview cards. Manual creation via existing TUI/API routes remains supported.

---

## Scope

GodAgent currently has 12 tools. It can create characters and plant foreshadowing, but lacks tools for:
- `create_scene` — scenes
- `create_chapter` — chapters/acts
- `create_arc` — story arcs
- `create_secret` — secrets (existing `expose_secret` reveals, not creates)
- `add_world_knowledge` — world knowledge (maps, history, magic systems, factions)
- `create_location` — locations

This spec adds these 6 tools + the creation confirmation flow.

---

## UX Decisions

| Decision | Choice |
|----------|--------|
| Confirmation UI | **Preview card** with structured display (not raw Allow/Deny) |
| Card actions | **Confirm** / **Deny** / **Modify & Confirm** |
| Batch creation | **One at a time** — each entity gets its own card in sequence |
| Underlying mechanism | New `creation_requested` / `creation_resolved` SSE events (separate from `approval_requested`) |

---

## Architecture: End-to-End Flow

```
GodAgent calls create_scene tool
        │
        ▼
WorldbuildingService::build_scene_preview()
  - Does NOT write to DB
  - Generates creation_id
  - Stores in pending_creations_ map
        │
        ▼
RuntimeService emits SSE: creation_requested
  payload: { creation_id, tool, preview: { ... } }
        │
        ▼
WebUI CreationCell renders preview card
  User: Confirm / Deny / Modify
        │
        ▼
POST /v1/creations/:id/resolve
  { decision: "allow" | "deny" | "modify", modifications: { ... } }
        │
        ▼
RuntimeService::resolve_creation()
  allow   → WorldbuildingService writes to DB
  deny    → discard
  modify  → merge modifications, then write
        │
        ▼
SSE: creation_resolved
  { creation_id, tool, decision, result: { ... } }
        │
        ▼
GodAgent continues (next creation or response message)
```

---

## New SSE Events

| Event | Direction | Payload |
|-------|-----------|---------|
| `creation_requested` | server → client | `{ creation_id, tool, preview: {...} }` |
| `creation_resolved` | server → client | `{ creation_id, tool, decision, result: {...} }` |

---

## New HTTP Endpoint

```
POST /v1/creations/:id/resolve

Request:
{
  "decision": "allow" | "deny" | "modify",
  "modifications": { ... }   // only for "modify", contains changed fields
}

Response (200):
{
  "ok": true,
  "creation_id": "...",
  "decision": "allow",
  "result": { "scene_id": "...", ... }
}
```

---

## Runtime Metadata Extension

`GET /v1/runtime` tools list extended with `requires_confirmation` field:

```json
{
  "name": "create_scene",
  "description": "Create a new scene in a world",
  "source": "worldbuilding",
  "requires_confirmation": true
}
```

Frontend uses this flag to render `CreationCell` for confirmation-requiring tools.

---

## Preview Card Schemas (per tool)

### create_character
```
name, gender, age, race, identity, emotional_tendency, speaking_style,
core_desire, deep_fear, daily_goal, background, knowledge_scope,
appearance, core_traits[], taboo_topics[]
```

### create_scene
```
title, chapter_id, world_time, narrative, participant_ids[],
location_id, section_id
```

### create_chapter
```
title, arc_id, order_index, summary
```

### create_arc
```
name, description, chapter_ids[]
```

### create_secret
```
content, holder_agent_ids[], discoverable_by[], difficulty
```

### add_world_knowledge
```
title, category (map|history|magic|faction|other), content, related_entity_ids[]
```

### create_location
```
name, description, region, parent_location_id
```

---

## WebUI: CreationCell Component

Per-tool preview card rendered as a structured card, not a raw JSON dump.

- **Header:** tool display name + icon
- **Body:** key fields rendered as labeled rows; long text fields (background, narrative) can be collapsed/expanded
- **Footer:** three buttons — Confirm (green), Modify (blue), Deny (gray)
- **Modify mode:** card switches to editable form; fields pre-filled with preview data; user edits and clicks "Confirm Modification"

Batch creation: cards appear sequentially. Processing one card reveals the next.

CSS: new `CreationCell.css`. Component lives at `webui/src/components/cells/CreationCell.tsx`.

---

## C++ Layer Changes

### 1. `libs/worldbuilding/src/worldbuilding_tools.cpp`

In `create_tools()`, for `AgentKind::God`, register 6 new tools. Each tool's `execute` callback:
1. Parses params from tool input
2. Calls `WorldbuildingService::build_*_preview()` to get structured preview
3. Generates `creation_id`, stores `PendingCreation` in service
4. Returns preview JSON + `creation_id` as tool result (or emits via a callback)

Tools marked with `requires_confirmation = true` in their metadata.

### 2. `libs/worldbuilding/include/merak/worldbuilding/worldbuilding_service.hpp`

```cpp
struct PendingCreation {
    std::string creation_id;
    std::string tool;
    std::string world_id;
    nlohmann::json params;
    nlohmann::json preview;
    std::chrono::steady_clock::time_point created_at;
};

// Preview builders (no DB write)
nlohmann::json build_scene_preview(const std::string& world_id, const Scene& scene);
nlohmann::json build_character_preview(const std::string& world_id, const CharacterCard& card);
nlohmann::json build_chapter_preview(const std::string& world_id, const Chapter& chapter);
nlohmann::json build_arc_preview(const std::string& world_id, const Arc& arc);
nlohmann::json build_secret_preview(const std::string& world_id, const Secret& secret);
nlohmann::json build_world_knowledge_preview(const std::string& world_id, const WorldKnowledge& knowledge);
nlohmann::json build_location_preview(const std::string& world_id, const Location& location);

// Resolution (writes to DB on allow/modify)
nlohmann::json resolve_creation(const std::string& creation_id,
                                const std::string& decision,
                                const nlohmann::json& modifications);

// Store
std::map<std::string, PendingCreation> pending_creations_;
```

### 3. `libs/runtime/include/merak/runtime_service.hpp`

```cpp
void submit_creation_request(const std::string& session_id, const CreationRequest& req);
void resolve_creation(const std::string& creation_id,
                      const std::string& decision,
                      const nlohmann::json& modifications);
```

### 4. `libs/http/src/http_server.cpp`

```cpp
server_.Post(R"(/v1/creations/([^/]+)/resolve)",
    [this](const auto& req, auto& res) { handle_creation_resolve(req, res); });
```

### 5. Agent Loop Integration

Tool execution → detect `requires_confirmation` → `CreationManager` suspends agent loop (similar to `await_approval()` pattern with `condition_variable`) → emits `creation_requested` SSE → waits for user → `resolve_creation` wakes the loop → tool result returned to agent.

---

## Existing vs. New: No Breaking Changes

- Existing `create_character` tool continues to work as before (no confirmation).
- Only the new `AgentKind::God` tools use the confirmation flow.
- Manual creation APIs (POST endpoints) remain unchanged — the confirmation flow is purely for GodAgent's conversational path.
- The existing `POST /v1/approvals/:id` endpoint and approval mechanism are NOT modified; creation confirmation is a parallel system.

---

## Testing

- Unit tests for each `build_*_preview()` — verify preview structure
- Unit tests for `resolve_creation()` — verify allow/deny/modify paths
- Integration test: tool call → SSE event → resolve → DB write
- Integration test: modify path — verify user modifications are merged correctly
- WebUI: test CreationCell rendering for each tool type
- WebUI: test modify mode form interactions

---

## Deferred / Out of Scope

- Bulk confirm (confirm all at once) — batch is confirmed one-by-one
- Edit-after-creation in the confirmation flow (use existing manual edit APIs instead)
- Tool result streaming confirmation (future optimization)
