# Session & World Naming Design

## Summary

Currently sessions display as truncated IDs (`session_171...`) and worlds, while having a `name` field set at creation, cannot be renamed. This design adds naming and renaming capabilities to both, with auto-naming for sessions and a consistent API across TUI and WebUI.

## Current State

- **Session**: `title` field exists in `SessionRecord` (C++), SQLite schema, API response, and `SessionSummary` (TypeScript), but is never populated. All callers pass empty string.
- **World**: `name` and `description` exist in `WorldMeta` and are set at creation, but no update endpoint exists.
- **TUI**: `SessionMeta` and `SessionEntry` have no `title` field.
- **UI fallback**: Both TUI and WebUI display truncated ID when title is absent.

## Design

### 1. Data Model Changes

**Session** — no structural change needed. `title` field already exists everywhere. Remove the empty-string default behavior.

**World** — no structural change needed. `name` and `description` already exist.

**TUI SessionMeta / SessionEntry** — add `std::string title` field to both structs.

### 2. Auto-Naming Logic (Sessions)

Server-side, inside `RuntimeService::start_run()`:

- After receiving the user message, check: `last_seq == 0` (first run of session) AND `title` is empty
- Extract plain text from user message: strip leading/trailing whitespace and newlines, take first 50 characters
- Call `SessionStore::update_title(session_id, extracted_title)`
- If the client explicitly passed a `title` at session creation, skip auto-naming (client title wins)

**Rationale**: Server-side ensures TUI and WebUI get uniform behavior with zero client-side logic. The check runs once per session (on first run), so overhead is negligible.

**Before first run**: Sessions with no title display as "New Session" in the UI.

### 3. New API Endpoints

#### PATCH /v1/sessions/:id

Update session title.

Request:
```json
{"title": "调试 PostgreSQL 连接池"}
```

Response: updated `SessionSummary`.

#### PATCH /api/worldbuilding/worlds/:id

Update world name and/or description.

Request:
```json
{"name": "赛博朋克 2077 世界观", "description": "一个高科技低生活的近未来世界设定"}
```

Both fields optional — only provided fields are updated. Response: updated `WorldMeta`.

### 4. AI Title Generation Endpoint

`POST /v1/sessions/:id/generate-title` — triggers a lightweight LLM call that summarizes the session's conversation history into a short title (max 20 chars). The endpoint returns the suggested title as plain text. The client then calls `PATCH /v1/sessions/:id` to apply it, giving the user a chance to review before confirming.

This is a dedicated endpoint rather than reusing the runs mechanism, to avoid polluting the session's conversation history with a technical title-generation turn.

### 5. WebUI Interactions

**Session list display**: Show `title` when present, otherwise "New Session". No more fallback to truncated ID.

**Rename**: Double-click session item → inline text input → Enter to confirm, Esc to cancel. Also available via right-click context menu → "Rename".

**AI title generation**: Hover over a session item → sparkle icon (✨) appears on the right → click triggers AI-generated title suggestion → result populates the inline edit field for user approval or rejection.

**World selector**: Edit icon (✎) next to world name in dropdown → opens a small modal with fields for `name` and `description` → Save/Cancel.

### 6. TUI Interactions

**Create session with title**:
```
/session new --title "调试认证错误"
```

**Rename session**:
```
/session rename <新名称>
```

**Session list**: `title` column takes visual priority over `id` column.

**World rename**:
```
/world rename <world_id> --name "新名称" --description "新描述"
```

Both `--name` and `--description` are optional; only provided ones update.

**TUI persistence**: `SessionMeta` gains a `title` field. On session creation and rename, the TUI syncs the title from the server.

### 7. Implementation Checklist

- [ ] Add `title` field to TUI `SessionMeta` and `SessionEntry` structs
- [ ] Add `update_title()` method to `SessionStore`
- [ ] Implement auto-naming in `RuntimeService::start_run()` (first run + empty title)
- [ ] Implement `PATCH /v1/sessions/:id` HTTP handler
- [ ] Implement `PATCH /api/worldbuilding/worlds/:id` HTTP handler
- [ ] Add `updateWorld()` to WebUI API client (`webui/src/api/`)
- [ ] Add inline rename UI to WebUI `SessionList` component
- [ ] Add context menu (right-click) to session items
- [ ] Add AI title generation trigger (✨ button) to session items
- [ ] Add world edit modal to `WorldSelector` component
- [ ] Add `/session rename` TUI command
- [ ] Add `/world rename` TUI command
- [ ] Add `--title` flag to `/session new` TUI command
- [ ] Sync `SessionMeta.title` in TUI persistence layer
- [ ] Update `SessionEntry` display in TUI resume view

### 8. Non-Goals

- World auto-naming (worlds are explicitly created, no ambiguity to resolve)
- Session title in SSE events or journal (out of scope)
- Batch rename (out of scope)
