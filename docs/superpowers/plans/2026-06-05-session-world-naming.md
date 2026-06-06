# Session & World Naming Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add naming/renaming for sessions (auto-title from first message + manual rename + AI summary) and worlds (rename + description update), surfaced in both WebUI and TUI.

**Architecture:** Server-side auto-naming in `RuntimeService::start_run()` on first run; new `PATCH` endpoints for session title and world name/description; WebUI inline-edit + context menu + sparkle button; TUI `/session rename` and `/world rename` commands.

**Tech Stack:** C++17 (httplib, SQLite3, nlohmann::json), TypeScript/React (WebUI), C++/ncurses (TUI)

---

### Task 1: Add `update_session` to SessionStore

**Files:**
- Modify: `libs/storage/include/merak/session_store.hpp`
- Modify: `libs/storage/src/session_store.cpp`

- [ ] **Step 1: Declare `update_session` in header**

In `libs/storage/include/merak/session_store.hpp`, add after `create_session` declaration (line 61):

```cpp
void update_session(const std::string& id, const std::string& title);
```

- [ ] **Step 2: Implement `update_session` in source**

In `libs/storage/src/session_store.cpp`, add after `create_session` implementation (after line 158):

```cpp
void SessionStore::update_session(const std::string& id, const std::string& title) {
    std::lock_guard lock(mutex_);
    sqlite3_stmt* s = nullptr;
    sqlite3_prepare_v2(db_,
        "UPDATE sessions SET title = ?, updated_at = ? WHERE id = ?",
        -1, &s, nullptr);
    bind_text(s, 1, title);
    bind_text(s, 2, now_iso());
    bind_text(s, 3, id);
    if (sqlite3_step(s) != SQLITE_DONE) {
        sqlite3_finalize(s);
        throw std::runtime_error("update session failed");
    }
    sqlite3_finalize(s);
}
```

- [ ] **Step 3: Build and verify**

```bash
cd build && cmake --build . --target merak_storage 2>&1 | tail -5
```

Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add libs/storage/include/merak/session_store.hpp libs/storage/src/session_store.cpp
git commit -m "feat: add update_session to SessionStore"
```

---

### Task 2: Add auto-naming in RuntimeService::start_run

**Files:**
- Modify: `libs/runtime/include/merak/runtime_service.hpp`
- Modify: `libs/runtime/src/runtime_service.cpp`

- [ ] **Step 1: Declare `update_session` and `extract_title` in header**

In `libs/runtime/include/merak/runtime_service.hpp`, add after `create_session` declaration (line 80):

```cpp
void update_session(const std::string& id, const std::string& title);
```

In the private section, add:

```cpp
static std::string extract_title(const std::string& message, size_t max_len = 50);
```

- [ ] **Step 2: Implement `extract_title` helper**

In `libs/runtime/src/runtime_service.cpp`, add near the top:

```cpp
std::string RuntimeService::extract_title(const std::string& message, size_t max_len) {
    // Strip leading/trailing whitespace
    size_t start = 0;
    while (start < message.size() && (message[start] == ' ' || message[start] == '\n' || message[start] == '\r' || message[start] == '\t'))
        start++;
    if (start >= message.size()) return "";

    size_t end = message.size();
    while (end > start && (message[end-1] == ' ' || message[end-1] == '\n' || message[end-1] == '\r' || message[end-1] == '\t'))
        end--;

    std::string trimmed = message.substr(start, end - start);

    // Take first line only
    size_t nl = trimmed.find('\n');
    if (nl != std::string::npos) trimmed = trimmed.substr(0, nl);

    // Truncate to max_len
    if (trimmed.size() > max_len) trimmed = trimmed.substr(0, max_len);

    return trimmed;
}
```

- [ ] **Step 3: Implement auto-naming in `create_run_record`**

In `libs/runtime/src/runtime_service.cpp`, modify `create_run_record` (after the existing emit line) to add auto-naming:

```cpp
RunRecord RuntimeService::create_run_record(const std::string& s, const std::string& m) {
    if (!store_.get_session(s)) throw RuntimeError("session_not_found", "session not found: " + s);
    if (store_.has_unfinished_run(s)) throw RuntimeError("session_busy", "session has an unfinished run");
    auto r = store_.create_run(s, m);
    emit(s, r.id, "run_started", {{"message", m}});

    // Auto-name session from first user message
    auto session = store_.get_session(s);
    if (session && session->last_seq == 0 && session->title.empty()) {
        std::string auto_title = extract_title(m);
        if (!auto_title.empty()) {
            store_.update_session(s, auto_title);
        }
    }

    return r;
}
```

- [ ] **Step 4: Implement `update_session` pass-through**

In `libs/runtime/src/runtime_service.cpp`:

```cpp
void RuntimeService::update_session(const std::string& id, const std::string& title) {
    store_.update_session(id, title);
    emit(id, "", "session_updated", {{"title", title}});
}
```

- [ ] **Step 5: Build and verify**

```bash
cd build && cmake --build . --target merak_runtime 2>&1 | tail -5
```

Expected: Build succeeds.

- [ ] **Step 6: Commit**

```bash
git add libs/runtime/include/merak/runtime_service.hpp libs/runtime/src/runtime_service.cpp
git commit -m "feat: add auto-naming and update_session to RuntimeService"
```

---

### Task 3: Add PATCH /v1/sessions/:id HTTP endpoint

**Files:**
- Modify: `libs/http/include/merak/http_server.hpp`
- Modify: `libs/http/src/http_server.cpp`

- [ ] **Step 1: Declare handler in header**

In `libs/http/include/merak/http_server.hpp`, add to the HttpServer class:

```cpp
HttpResult handle_update_session(const std::string& id, const std::string& title);
```

- [ ] **Step 2: Implement handler**

In `libs/http/src/http_server.cpp`, add:

```cpp
HttpResult HttpServer::handle_update_session(const std::string& id, const std::string& title) {
    runtime_->update_session(id, title);
    auto s = runtime_->get_session(id);
    if (!s) return {404, {{"error", "session not found"}}};
    return {200, {{"session", session_json(*s)}}};
}
```

- [ ] **Step 3: Register route**

In `libs/http/src/http_server.cpp`, in `install_routes`, add after the GET session route (after line 49):

```cpp
server_.Patch(R"(/v1/sessions/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
    auto id = req.matches[1];
    auto body = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
    std::string title = body.value("title", "");
    json_response(res, handle_update_session(id, title));
});
```

- [ ] **Step 4: Build and verify**

```bash
cd build && cmake --build . --target merak_http 2>&1 | tail -5
```

Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add libs/http/include/merak/http_server.hpp libs/http/src/http_server.cpp
git commit -m "feat: add PATCH /v1/sessions/:id endpoint for title update"
```

---

### Task 4: Add POST /v1/sessions/:id/generate-title endpoint

**Files:**
- Modify: `libs/http/include/merak/http_server.hpp`
- Modify: `libs/http/src/http_server.cpp`
- Modify: `libs/runtime/include/merak/runtime_service.hpp`
- Modify: `libs/runtime/src/runtime_service.cpp`

- [ ] **Step 1: Declare and implement `generate_title` in RuntimeService**

In `libs/runtime/include/merak/runtime_service.hpp`, add declaration:

```cpp
std::string generate_title(const std::string& session_id);
```

In `libs/runtime/src/runtime_service.cpp`, add implementation:

```cpp
std::string RuntimeService::generate_title(const std::string& session_id) {
    // Read the last few user messages from the session journal as context
    auto events = store_.get_events(session_id);
    std::string context;
    int user_msgs = 0;
    for (auto it = events.rbegin(); it != events.rend() && user_msgs < 3; ++it) {
        if (it->type == "user") {
            context = it->payload.value("message", "") + "\n" + context;
            user_msgs++;
        }
    }
    if (context.empty()) return "";

    // Build a lightweight summarization prompt
    std::string prompt = "Based on these user messages, generate a short title (max 20 characters) "
                         "that summarizes the conversation topic. Reply with ONLY the title, nothing else.\n\n"
                         + context;

    // Use the loop factory to make a single-turn completion
    if (!loop_factory_) return "";
    auto loop = loop_factory_();
    auto result = loop->complete(prompt);
    // Trim to max 50 chars
    if (result.size() > 50) result = result.substr(0, 50);
    return result;
}
```

- [ ] **Step 2: Register route**

In `libs/http/src/http_server.cpp`, in `install_routes`, add after the PATCH session route (from Task 3):

```cpp
server_.Post(R"(/v1/sessions/([^/]+)/generate-title)", [this](const httplib::Request& req, httplib::Response& res) {
    auto id = req.matches[1];
    try {
        std::string title = runtime_->generate_title(id);
        json_response(res, {{"title", title}});
    } catch (const std::exception& e) {
        json_response(res, {{"error", e.what()}}, 500);
    }
});
```

- [ ] **Step 3: Build and verify**

```bash
cd build && cmake --build . --target merak_http 2>&1 | tail -10
```

Expected: Build succeeds. May need to add `get_events` or equivalent method to SessionStore if not already exposed.

- [ ] **Step 4: Commit**

```bash
git add libs/runtime/include/merak/runtime_service.hpp libs/runtime/src/runtime_service.cpp libs/http/include/merak/http_server.hpp libs/http/src/http_server.cpp
git commit -m "feat: add POST /v1/sessions/:id/generate-title endpoint"
```

---

### Task 5: Add `update_world` to WorldStore

**Files:**
- Modify: `libs/worldbuilding/include/merak/worldbuilding/world_store.hpp`
- Modify: `libs/worldbuilding/src/world_store.cpp`

- [ ] **Step 1: Declare `update_world` in header**

In `libs/worldbuilding/include/merak/worldbuilding/world_store.hpp`, add after `create_world` declaration:

```cpp
WorldMeta update_world(const std::string& world_id,
                       const std::optional<std::string>& name,
                       const std::optional<std::string>& description);
```

- [ ] **Step 2: Implement `update_world`**

In `libs/worldbuilding/src/world_store.cpp`, add after `create_world`:

```cpp
WorldMeta WorldStore::update_world(const std::string& world_id,
                                    const std::optional<std::string>& name,
                                    const std::optional<std::string>& description) {
    initialize();
    auto existing = get_world(world_id);
    if (!existing) throw std::runtime_error("world not found: " + world_id);

    std::string new_name = name.value_or(existing->name);
    std::string new_desc = description.value_or(existing->description);
    std::string timestamp = now_iso_utc();

    auto conn = pool_->acquire();
    pqxx::work txn(*conn);
    txn.exec_params("UPDATE worlds SET name = $1, description = $2, updated_at = $3 WHERE id = $4",
                    new_name, new_desc, timestamp, world_id);
    txn.commit();

    return WorldMeta{world_id, new_name, new_desc, existing->created_at, timestamp};
}
```

- [ ] **Step 3: Build and verify**

```bash
cd build && cmake --build . --target merak_worldbuilding 2>&1 | tail -5
```

Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add libs/worldbuilding/include/merak/worldbuilding/world_store.hpp libs/worldbuilding/src/world_store.cpp
git commit -m "feat: add update_world to WorldStore"
```

---

### Task 6: Add `update_world` to WorldbuildingService

**Files:**
- Modify: `libs/worldbuilding/include/merak/worldbuilding/worldbuilding_service.hpp`
- Modify: `libs/worldbuilding/src/worldbuilding_service.cpp`

- [ ] **Step 1: Declare in header**

In `libs/worldbuilding/include/merak/worldbuilding/worldbuilding_service.hpp`, add after `create_world`:

```cpp
WorldMeta update_world(const std::string& world_id,
                       const std::optional<std::string>& name,
                       const std::optional<std::string>& description);
```

- [ ] **Step 2: Implement pass-through**

In `libs/worldbuilding/src/worldbuilding_service.cpp`, add:

```cpp
WorldMeta WorldbuildingService::update_world(const std::string& world_id,
                                              const std::optional<std::string>& name,
                                              const std::optional<std::string>& description) {
    return worlds_.update_world(world_id, name, description);
}
```

- [ ] **Step 3: Build and verify**

```bash
cd build && cmake --build . --target merak_worldbuilding 2>&1 | tail -5
```

Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add libs/worldbuilding/include/merak/worldbuilding/worldbuilding_service.hpp libs/worldbuilding/src/worldbuilding_service.cpp
git commit -m "feat: add update_world to WorldbuildingService"
```

---

### Task 7: Add PATCH /api/worldbuilding/worlds/:id endpoint

**Files:**
- Modify: `libs/http/include/merak/worldbuilding_http_handler.hpp`
- Modify: `libs/http/src/worldbuilding_http_handler.cpp`

- [ ] **Step 1: Declare static handler in header**

In `libs/http/include/merak/worldbuilding_http_handler.hpp`, add:

```cpp
static void handle_update_world(const httplib::Request& req, httplib::Response& res);
```

- [ ] **Step 2: Implement handler**

In `libs/http/src/worldbuilding_http_handler.cpp`, add:

```cpp
void WorldbuildingHttpHandler::handle_update_world(const httplib::Request& req, httplib::Response& res) {
    auto world_id = req.matches[1];
    auto body = nlohmann::json::parse(req.body);
    std::optional<std::string> name;
    std::optional<std::string> description;
    if (body.contains("name")) name = body["name"].get<std::string>();
    if (body.contains("description")) description = body["description"].get<std::string>();

    try {
        auto world = service_->update_world(world_id, name, description);
        json_response(res, {{"ok", true}, {"world_id", world.id}, {"name", world.name}, {"description", world.description}});
    } catch (const std::exception& e) {
        json_response(res, {{"error", e.what()}}, 404);
    }
}
```

- [ ] **Step 3: Register route**

In `libs/http/src/worldbuilding_http_handler.cpp`, in `install_routes`, add after the DELETE world route (after line 29):

```cpp
server.Patch(R"(/api/worldbuilding/worlds/([^/]+))", handle_update_world);
```

- [ ] **Step 4: Build and verify**

```bash
cd build && cmake --build . --target merak_http 2>&1 | tail -5
```

Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add libs/http/include/merak/worldbuilding_http_handler.hpp libs/http/src/worldbuilding_http_handler.cpp
git commit -m "feat: add PATCH /api/worldbuilding/worlds/:id endpoint"
```

---

### Task 8: Add updateSession, updateWorld, generateTitle to WebUI API client

**Files:**
- Modify: `webui/src/api/client.ts`

- [ ] **Step 1: Add new API methods**

In `webui/src/api/client.ts`, add after `createSession` (line 28):

```typescript
updateSession: (id: string, title: string) =>
    request('PATCH', `/v1/sessions/${id}`, { title }),

generateTitle: (id: string) =>
    request('POST', `/v1/sessions/${id}/generate-title`),
```

Add after `listWorlds` (line 54):

```typescript
updateWorld: (id: string, name?: string, description?: string) =>
    request('PATCH', `/api/worldbuilding/worlds/${id}`, { name, description }),
```

- [ ] **Step 2: Build WebUI**

```bash
cd webui && npx tsc --noEmit 2>&1 | head -20
```

Expected: No new type errors.

- [ ] **Step 3: Commit**

```bash
git add webui/src/api/client.ts
git commit -m "feat: add updateSession and updateWorld to WebUI API client"
```

---

### Task 9: Add inline rename UI to WebUI SessionList

**Files:**
- Modify: `webui/src/components/Sidebar/SessionList.tsx`

- [ ] **Step 1: Rewrite SessionList with rename and context menu**

Replace the content of `webui/src/components/Sidebar/SessionList.tsx` with:

```tsx
import { useState } from 'react';
import { useAppState } from '../../AppState';
import { api } from '../../api/client';

export default function SessionList() {
  const { state, dispatch } = useAppState();
  const [editingId, setEditingId] = useState<string | null>(null);
  const [editValue, setEditValue] = useState('');

  async function create() {
    const data = await api.createSession();
    const id = data.session_id;
    dispatch({
      type: 'SET_SESSIONS',
      sessions: [
        ...state.sessions,
        { id, title: '', last_seq: 0, created_at: '', updated_at: '', archived_at: null },
      ],
    });
    select(id);
  }

  function select(id: string) {
    dispatch({ type: 'SET_SESSION', sessionId: id });
  }

  function startRename(session: { id: string; title: string }) {
    setEditingId(session.id);
    setEditValue(session.title || '');
  }

  function cancelRename() {
    setEditingId(null);
    setEditValue('');
  }

  async function confirmRename(id: string) {
    const newTitle = editValue.trim();
    if (newTitle) {
      const data = await api.updateSession(id, newTitle);
      const updated = data.session;
      dispatch({
        type: 'SET_SESSIONS',
        sessions: state.sessions.map((s) =>
          s.id === id ? { ...s, title: updated.title, updated_at: updated.updated_at } : s
        ),
      });
    }
    setEditingId(null);
    setEditValue('');
  }

  async function generateTitle(id: string) {
    try {
      const res = await api.generateTitle(id);
      const title = res.title;
      if (title) {
        const data = await api.updateSession(id, title);
        const updated = data.session;
        dispatch({
          type: 'SET_SESSIONS',
          sessions: state.sessions.map((s) =>
            s.id === id ? { ...s, title: updated.title, updated_at: updated.updated_at } : s
          ),
        });
      }
    } catch {
      // fallback: open rename input on failure
      startRename(state.sessions.find((s) => s.id === id) || { id, title: '' });
    }
  }

  const sessions = [...state.sessions].sort(
    (a, b) => new Date(b.updated_at || b.created_at).getTime() - new Date(a.updated_at || a.created_at).getTime()
  );

  return (
    <div className="session-list">
      <div className="session-list-header">
        <span>Sessions</span>
        <button onClick={create} title="New Session">+</button>
      </div>
      <ul>
        {sessions.map((s) => (
          <li
            key={s.id}
            className={s.id === state.sessionId ? 'active' : ''}
            onClick={() => select(s.id)}
            onContextMenu={(e) => { e.preventDefault(); startRename(s); }}
          >
            {editingId === s.id ? (
              <input
                className="session-rename-input"
                value={editValue}
                onChange={(e) => setEditValue(e.target.value)}
                onBlur={() => confirmRename(s.id)}
                onKeyDown={(e) => {
                  if (e.key === 'Enter') confirmRename(s.id);
                  if (e.key === 'Escape') cancelRename();
                }}
                onClick={(e) => e.stopPropagation()}
                autoFocus
              />
            ) : (
              <>
                <span
                  className="session-title"
                  onDoubleClick={(e) => { e.stopPropagation(); startRename(s); }}
                  aria-label={s.title || 'New Session'}
                >
                  {s.title || 'New Session'}
                </span>
                {s.id === state.sessionId && (
                  <button
                    className="session-generate-btn"
                    title="Generate title"
                    onClick={(e) => { e.stopPropagation(); generateTitle(s.id); }}
                  >
                    ✨
                  </button>
                )}
              </>
            )}
          </li>
        ))}
      </ul>
    </div>
  );
}
```

- [ ] **Step 2: Add minimal CSS for rename input**

In `webui/src/components/Sidebar/SessionList.css` (create if not exists):

```css
.session-rename-input {
  width: 100%;
  padding: 2px 4px;
  border: 1px solid var(--border-color, #555);
  border-radius: 3px;
  background: var(--bg-input, #1a1a1a);
  color: var(--text-color, #eee);
  font-size: inherit;
}

.session-generate-btn {
  background: none;
  border: none;
  cursor: pointer;
  opacity: 0.6;
  font-size: 0.85em;
  padding: 0 4px;
}
.session-generate-btn:hover {
  opacity: 1;
}
```

- [ ] **Step 3: Build and check**

```bash
cd webui && npx tsc --noEmit 2>&1 | head -20
```

Expected: No new type errors.

- [ ] **Step 4: Commit**

```bash
git add webui/src/components/Sidebar/SessionList.tsx webui/src/components/Sidebar/SessionList.css
git commit -m "feat: add inline rename, context menu, and AI title trigger to SessionList"
```

---

### Task 10: Add world edit modal to WorldSelector

**Files:**
- Modify: `webui/src/components/Sidebar/WorldSelector.tsx`

- [ ] **Step 1: Rewrite WorldSelector with edit modal**

Replace the content of `webui/src/components/Sidebar/WorldSelector.tsx` with:

```tsx
import { useState } from 'react';
import { useAppState } from '../../AppState';
import { api } from '../../api/client';

export default function WorldSelector() {
  const { state, dispatch } = useAppState();
  const { worlds } = state;
  const [editWorld, setEditWorld] = useState<{ id: string; name: string; description: string } | null>(null);

  function openEdit(worldId: string) {
    const w = worlds.find((w) => w.id === worldId);
    if (w) setEditWorld({ id: w.id, name: w.name || '', description: w.description || '' });
  }

  async function saveEdit() {
    if (!editWorld) return;
    await api.updateWorld(editWorld.id, editWorld.name, editWorld.description);
    dispatch({
      type: 'SET_WORLDS',
      worlds: worlds.map((w) =>
        w.id === editWorld.id ? { ...w, name: editWorld.name, description: editWorld.description } : w
      ),
    });
    setEditWorld(null);
  }

  return (
    <div className="world-selector">
      <select
        value={state.worldId ?? ''}
        onChange={(e) => dispatch({ type: 'SET_WORLD', worldId: e.target.value || null })}
      >
        <option value="">None</option>
        {worlds.map((world) => (
          <option key={world.id} value={world.id}>
            {world.name || world.id}
          </option>
        ))}
      </select>
      {state.worldId && (
        <button className="world-edit-btn" onClick={() => openEdit(state.worldId!)} title="Edit world">
          ✎
        </button>
      )}

      {editWorld && (
        <div className="modal-overlay" onClick={() => setEditWorld(null)}>
          <div className="modal" onClick={(e) => e.stopPropagation()}>
            <h3>Edit World</h3>
            <label>
              Name
              <input
                value={editWorld.name}
                onChange={(e) => setEditWorld({ ...editWorld, name: e.target.value })}
              />
            </label>
            <label>
              Description
              <textarea
                value={editWorld.description}
                onChange={(e) => setEditWorld({ ...editWorld, description: e.target.value })}
                rows={3}
              />
            </label>
            <div className="modal-actions">
              <button onClick={saveEdit}>Save</button>
              <button onClick={() => setEditWorld(null)}>Cancel</button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
```

- [ ] **Step 2: Build and check**

```bash
cd webui && npx tsc --noEmit 2>&1 | head -20
```

Expected: No new type errors.

- [ ] **Step 3: Commit**

```bash
git add webui/src/components/Sidebar/WorldSelector.tsx
git commit -m "feat: add world edit modal to WorldSelector"
```

---

### Task 11: Add title field to TUI SessionMeta and SessionEntry

**Files:**
- Modify: `cli/src/tui/persistence/turn_event.hpp`

- [ ] **Step 1: Add `title` to SessionMeta**

In `cli/src/tui/persistence/turn_event.hpp`, in `SessionMeta` struct (line 63), add after `session_id`:

```cpp
struct SessionMeta {
    std::string session_id;
    std::string title;          // <-- add this line
    uint64_t created_at = 0;
    // ... rest unchanged ...
};
```

- [ ] **Step 2: Add `title` to SessionEntry** (if defined in resume_view.hpp)

Check `cli/src/tui/overlay/resume_view.hpp` for `SessionEntry` struct and add:

```cpp
std::string title;
```

after the `sid` field.

- [ ] **Step 3: Build TUI**

```bash
cd build && cmake --build . --target merak_cli 2>&1 | tail -10
```

Expected: Build succeeds (may need to fix any serialization code that reads/writes SessionMeta).

- [ ] **Step 4: Commit**

```bash
git add cli/src/tui/persistence/turn_event.hpp cli/src/tui/overlay/resume_view.hpp
git commit -m "feat: add title field to TUI SessionMeta and SessionEntry"
```

---

### Task 12: Add /session rename and --title flag to TUI

**Files:**
- Modify: `cli/src/main.cpp`

- [ ] **Step 1: Add `/session rename` command parsing**

In `cli/src/main.cpp`, in the `run_tui` function, find the session command handler (around line 337). Add a `rename` subcommand:

```cpp
} else if (cmd == "rename") {
    std::string new_title = trim(rest);
    if (new_title.empty()) {
        add_message("Usage: /session rename <new title>");
    } else {
        auto res = api.patch("/v1/sessions/" + current_session_id,
                             {{"title", new_title}});
        add_message("Session renamed to: " + new_title);
    }
}
```

- [ ] **Step 2: Add `--title` flag to `/session new`**

Find the `/session new` handler and add title support:

```cpp
} else if (cmd == "new") {
    std::string title;
    // Parse --title flag if present
    auto title_pos = rest.find("--title");
    if (title_pos != std::string::npos) {
        title = trim(rest.substr(title_pos + 7)); // after "--title"
        // Remove the flag from rest if needed
    }
    auto res = title.empty()
        ? api.create_session()
        : api.post("/v1/sessions", {{"title", title}});
    current_session_id = res["session_id"];
    // ... rest of new session logic ...
}
```

- [ ] **Step 3: Update session list display**

In the `/session list` handler, show `title` prominently:

```cpp
} else if (cmd == "list") {
    auto sessions = api.list_sessions();
    for (auto& s : sessions["sessions"]) {
        std::string title = s.value("title", "");
        std::string id = s["id"];
        add_message((title.empty() ? "New Session" : title) + "  [" + id + "]");
    }
}
```

- [ ] **Step 4: Build and verify**

```bash
cd build && cmake --build . --target merak_cli 2>&1 | tail -10
```

Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add cli/src/main.cpp
git commit -m "feat: add /session rename and --title flag to TUI"
```

---

### Task 13: Add /world rename command to TUI

**Files:**
- Modify: `cli/src/commands/worldbuilding_commands.cpp`

- [ ] **Step 1: Add WorldRename action to parser**

Find the world command parsing (around line 76) and add:

```cpp
} else if (subcmd == "rename") {
    cmd.action = WorldbuildingAction::WorldRename;
    // args[0] = world_id, remaining args parsed for --name and --description
}
```

- [ ] **Step 2: Add WorldRename to action enum**

In `cli/src/commands/worldbuilding_commands.hpp`:

```cpp
enum class WorldbuildingAction {
    // ... existing ...
    WorldRename,
};
```

- [ ] **Step 3: Implement execution**

In the action switch, add:

```cpp
case WorldbuildingAction::WorldRename: {
    std::string world_id = cmd.args.empty() ? "" : cmd.args[0];
    if (world_id.empty()) {
        return {{"error", "Usage: /world rename <world_id> --name <name> --description <desc>"}};
    }
    nlohmann::json body;
    // Parse --name and --description from remaining args
    for (size_t i = 1; i < cmd.args.size(); i++) {
        if (cmd.args[i] == "--name" && i + 1 < cmd.args.size()) {
            body["name"] = cmd.args[++i];
        } else if (cmd.args[i] == "--description" && i + 1 < cmd.args.size()) {
            body["description"] = cmd.args[++i];
        }
    }
    return patch("/api/worldbuilding/worlds/" + world_id, body);
}
```

- [ ] **Step 4: Build and verify**

```bash
cd build && cmake --build . --target merak_cli 2>&1 | tail -10
```

Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add cli/src/commands/worldbuilding_commands.cpp cli/src/commands/worldbuilding_commands.hpp
git commit -m "feat: add /world rename command to TUI"
```

---

### Task 14: Sync SessionMeta.title in TUI persistence + cleanup

**Files:**
- Modify: `cli/src/tui/persistence/turn_event.hpp` (or the persistence writer)
- Modify: `cli/src/main.cpp`

- [ ] **Step 1: Persist title on session create**

In `cli/src/main.cpp`, when writing the initial `SessionMeta` event after creating/connecting to a session, include the title:

```cpp
SessionMeta meta;
meta.session_id = current_session_id;
meta.title = session_json.value("title", "");  // <-- add this
meta.created_at = /* ... */;
// ... write meta to journal ...
```

- [ ] **Step 2: Update SessionEntry display in resume view**

In `cli/src/tui/overlay/resume_view.hpp`, modify the session entry rendering to show title first:

```cpp
std::string label = entry.title.empty()
    ? entry.sid.substr(0, 12) + "..."
    : entry.title;
// Render label instead of entry.sid
```

- [ ] **Step 3: Build and verify**

```bash
cd build && cmake --build . --target merak_cli 2>&1 | tail -10
```

Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add cli/src/main.cpp cli/src/tui/overlay/resume_view.hpp
git commit -m "feat: sync session title in TUI persistence and resume view"
```

---

### Verification Checklist

After all tasks are complete:

1. **Start server**, create session via WebUI → session appears as "New Session"
2. Send first message → title auto-updates to first 50 chars of message
3. Double-click session → inline rename → title updates
4. Right-click session → context menu triggers rename
5. Click ✨ on active session → AI title generation triggers
6. Create session with `--title` in TUI → title is set, not auto-overwritten
7. `/session rename "New Name"` in TUI → title updates
8. `/session list` in TUI → shows titles
9. Edit world name/description via WebUI modal → saves correctly
10. `/world rename <id> --name "X" --description "Y"` in TUI → updates
11. `PATCH /v1/sessions/:id` returns updated SessionSummary
12. `PATCH /api/worldbuilding/worlds/:id` returns updated WorldMeta
