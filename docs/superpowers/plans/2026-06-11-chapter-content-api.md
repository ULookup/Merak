# Chapter Content API — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `content` field to Chapter model, implement `GET /api/worldbuilding/:wid/chapters/:cid`, and fix `PATCH` to persist content.

**Architecture:** One field flows through five layers — struct → JSON serde → NarrativeStore (CRUD) → HTTP handler → frontend. The handler delegates to NarrativeStore via WorldbuildingService, following existing patterns exactly.

**Tech Stack:** C++17, nlohmann::json, httplib, PostgreSQL (libpq), React/TypeScript frontend (no changes needed)

---

### Task 1: Add `content` field to Chapter struct

**Files:**
- Modify: `libs/worldbuilding/include/merak/worldbuilding/world_models.hpp:179-186`

- [ ] **Step 1: Add `content` to Chapter struct**

```cpp
struct Chapter {
    std::string id, title, pitch, notes, content;  // content 新增
    int number = 0;
    std::optional<std::string> arc_id;
    ChapterStatus status = ChapterStatus::Outline;
    nlohmann::json emotional_curve = nlohmann::json::array();
    std::vector<std::string> scene_ids, foreshadowing_planted, foreshadowing_paid;
};
```

- [ ] **Step 2: Build to verify struct change compiles**

```bash
cd build && cmake --build . --target merak_worldbuilding 2>&1 | tail -5
```

Expected: compilation succeeds (no usage of `content` yet, so nothing breaks).

- [ ] **Step 3: Commit**

```bash
git add libs/worldbuilding/include/merak/worldbuilding/world_models.hpp
git commit -m "feat(worldbuilding): add content field to Chapter struct"
```

---

### Task 2: Update JSON serialization for `content`

**Files:**
- Modify: `libs/worldbuilding/src/narrative_store.cpp:98-114` (chapter_json)
- Modify: `libs/worldbuilding/src/narrative_store.cpp:116-134` (chapter_from_json)

- [ ] **Step 1: Add `content` to `chapter_json()`**

In `chapter_json()` at line 98, add `content` to the initializer list. Insert after `{"notes", chapter.notes},`:

```cpp
nlohmann::json chapter_json(const Chapter& chapter) {
    nlohmann::json json{{"id", chapter.id},
                        {"title", chapter.title},
                        {"pitch", chapter.pitch},
                        {"notes", chapter.notes},
                        {"content", chapter.content},       // 新增
                        {"number", chapter.number},
                        {"status", to_string(chapter.status)},
                        {"emotional_curve", chapter.emotional_curve},
                        {"scene_ids", chapter.scene_ids},
                        {"foreshadowing_planted",
                         chapter.foreshadowing_planted},
                        {"foreshadowing_paid", chapter.foreshadowing_paid}};
    json["arc_id"] = chapter.arc_id.has_value() ?
                         nlohmann::json(*chapter.arc_id) :
                         nlohmann::json(nullptr);
    return json;
}
```

- [ ] **Step 2: Add `content` to `chapter_from_json()`**

In `chapter_from_json()` at line 116, add content deserialization after `chapter.notes = ...`:

```cpp
Chapter chapter_from_json(const nlohmann::json& json) {
    Chapter chapter;
    chapter.id = json.at("id").get<std::string>();
    chapter.title = json.at("title").get<std::string>();
    chapter.pitch = json.at("pitch").get<std::string>();
    chapter.notes = json.at("notes").get<std::string>();
    chapter.content = json.value("content", "");             // 新增，用 value() 兼容旧数据
    chapter.number = json.at("number").get<int>();
    if (!json.at("arc_id").is_null()) {
        chapter.arc_id = json.at("arc_id").get<std::string>();
    }
    chapter.status =
        chapter_status_from_string(json.at("status").get<std::string>());
    chapter.emotional_curve = json.at("emotional_curve");
    chapter.scene_ids = json.at("scene_ids").get<std::vector<std::string>>();
    chapter.foreshadowing_planted =
        json.at("foreshadowing_planted").get<std::vector<std::string>>();
    chapter.foreshadowing_paid =
        json.at("foreshadowing_paid").get<std::vector<std::string>>();
    return chapter;
}
```

> 使用 `json.value("content", "")` 而非 `json.at("content")`：at() 在 key 不存在时抛异常，value() 返回默认值空字符串，兼容旧 JSON 文件中没有 content 字段的历史数据。

- [ ] **Step 3: Build**

```bash
cd build && cmake --build . --target merak_worldbuilding 2>&1 | tail -5
```

Expected: success.

- [ ] **Step 4: Commit**

```bash
git add libs/worldbuilding/src/narrative_store.cpp
git commit -m "feat(worldbuilding): serialize/deserialize content field in chapter JSON"
```

---

### Task 3: Update `create_chapter` to persist `content`

**Files:**
- Modify: `libs/worldbuilding/src/narrative_store.cpp:331-336` (INSERT statement)

- [ ] **Step 1: Add `content` column to INSERT SQL**

Current code at line 331:
```cpp
conn.query(
    "INSERT INTO chapters(id, world_id, arc_id, name, pitch, status, position)"
    " VALUES($1, $2, $3, $4, $5, $6, $7)",
    {chapter.id, world_id, chapter.arc_id.value_or(""),
     chapter.title, chapter.pitch, to_string(chapter.status),
     std::to_string(chapter.number)});
```

Change to:
```cpp
conn.query(
    "INSERT INTO chapters(id, world_id, arc_id, name, pitch, content, status, position)"
    " VALUES($1, $2, $3, $4, $5, $6, $7, $8)",
    {chapter.id, world_id, chapter.arc_id.value_or(""),
     chapter.title, chapter.pitch, chapter.content,
     to_string(chapter.status),
     std::to_string(chapter.number)});
```

- [ ] **Step 2: Build**

```bash
cd build && cmake --build . --target merak_worldbuilding 2>&1 | tail -5
```

Expected: success (assuming DB migration already applied; if build-time SQL validation exists, may need migration first — see Task 9 below).

- [ ] **Step 3: Commit**

```bash
git add libs/worldbuilding/src/narrative_store.cpp
git commit -m "feat(worldbuilding): persist content field in create_chapter"
```

---

### Task 4: Update `patch_chapter` to handle `content`

**Files:**
- Modify: `libs/worldbuilding/src/narrative_store.cpp:619-679`

- [ ] **Step 1: Add `content` to JSON file update**

In `patch_chapter()`, after line 632 (`if (fields.contains("notes")) ...`), add:

```cpp
if (fields.contains("content")) json["content"] = fields["content"];
```

- [ ] **Step 2: Add `content` to DB UPDATE SET clause**

In the DB update section (lines 639-677), after the `if (fields.contains("pitch"))` block, add:

```cpp
if (fields.contains("content")) {
    set_parts.push_back("content = $" + std::to_string(param_idx++));
    params.push_back(fields["content"].get<std::string>());
}
```

The full patch_chapter method with changes looks like:

```cpp
bool NarrativeStore::patch_chapter(const std::string& world_id,
                                    const std::string& chapter_id,
                                    const nlohmann::json& fields) {
    ensure_world_exists(worlds_, world_id);

    const auto path =
        worlds_.world_path(world_id) / "chapters" / (chapter_id + ".json");
    if (!std::filesystem::exists(path)) return false;
    auto json = read_json(path);

    // Update JSON fields
    if (fields.contains("title")) json["title"] = fields["title"];
    if (fields.contains("pitch")) json["pitch"] = fields["pitch"];
    if (fields.contains("notes")) json["notes"] = fields["notes"];
    if (fields.contains("content")) json["content"] = fields["content"];  // 新增
    if (fields.contains("number")) json["number"] = fields["number"];
    if (fields.contains("status")) json["status"] = fields["status"];

    write_json(path, json);

    // Build dynamic SET clause for DB update
    std::vector<std::string> set_parts;
    std::vector<std::string> params;
    int param_idx = 1;

    if (fields.contains("title")) {
        set_parts.push_back("name = $" + std::to_string(param_idx++));
        params.push_back(fields["title"].get<std::string>());
    }
    if (fields.contains("pitch")) {
        set_parts.push_back("pitch = $" + std::to_string(param_idx++));
        params.push_back(fields["pitch"].get<std::string>());
    }
    if (fields.contains("content")) {                                    // 新增
        set_parts.push_back("content = $" + std::to_string(param_idx++));
        params.push_back(fields["content"].get<std::string>());
    }
    if (fields.contains("status")) {
        set_parts.push_back("status = $" + std::to_string(param_idx++));
        params.push_back(fields["status"].get<std::string>());
    }
    if (fields.contains("number")) {
        set_parts.push_back("position = $" + std::to_string(param_idx++));
        if (fields["number"].is_number()) {
            params.push_back(std::to_string(fields["number"].get<int>()));
        } else {
            params.push_back(fields["number"].get<std::string>());
        }
    }

    if (set_parts.empty()) return true;

    std::string sql = "UPDATE chapters SET ";
    for (size_t i = 0; i < set_parts.size(); i++) {
        if (i > 0) sql += ", ";
        sql += set_parts[i];
    }
    sql += ", updated_at = NOW() WHERE id = $" + std::to_string(param_idx++) +
           " AND world_id = $" + std::to_string(param_idx++);
    params.push_back(chapter_id);
    params.push_back(world_id);

    PgConn conn(*pool_);
    conn.query(sql, params);

    return true;
}
```

- [ ] **Step 3: Build**

```bash
cd build && cmake --build . --target merak_worldbuilding 2>&1 | tail -5
```

Expected: success.

- [ ] **Step 4: Commit**

```bash
git add libs/worldbuilding/src/narrative_store.cpp
git commit -m "feat(worldbuilding): persist content field in patch_chapter"
```

---

### Task 5: Add `get_chapter` to NarrativeStore

**Files:**
- Modify: `libs/worldbuilding/include/merak/worldbuilding/narrative_store.hpp` (add declaration)
- Modify: `libs/worldbuilding/src/narrative_store.cpp` (add implementation)

- [ ] **Step 1: Add declaration to header**

In `narrative_store.hpp`, add after `chapter_context()` declaration (line 73):

```cpp
std::optional<Chapter> get_chapter(const std::string& world_id,
                                   const std::string& chapter_id) const;
```

- [ ] **Step 2: Add implementation to .cpp**

Add after `chapter_context()` implementation (around line 711), before the private methods:

```cpp
std::optional<Chapter>
NarrativeStore::get_chapter(const std::string& world_id,
                            const std::string& chapter_id) const {
    ensure_world_exists(worlds_, world_id);
    const auto path =
        worlds_.world_path(world_id) / "chapters" / (chapter_id + ".json");
    if (!std::filesystem::exists(path)) return std::nullopt;
    return chapter_from_json(read_json(path));
}
```

- [ ] **Step 3: Build**

```bash
cd build && cmake --build . --target merak_worldbuilding 2>&1 | tail -5
```

Expected: success.

- [ ] **Step 4: Commit**

```bash
git add libs/worldbuilding/include/merak/worldbuilding/narrative_store.hpp libs/worldbuilding/src/narrative_store.cpp
git commit -m "feat(worldbuilding): add NarrativeStore::get_chapter()"
```

---

### Task 6: Add `handle_get_chapter` declaration to HTTP handler header

**Files:**
- Modify: `libs/http/include/merak/worldbuilding_http_handler.hpp`

- [ ] **Step 1: Add declaration**

In the "Narrative" section of the class, add after `handle_list_chapters`:

```cpp
void handle_get_chapter(const httplib::Request&, httplib::Response&);
```

The full Narrative section becomes:
```cpp
// Narrative
void handle_overview(const httplib::Request&, httplib::Response&);
void handle_list_chapters(const httplib::Request&, httplib::Response&);
void handle_get_chapter(const httplib::Request&, httplib::Response&);
void handle_list_scenes(const httplib::Request&, httplib::Response&);
void handle_scene_new(const httplib::Request&, httplib::Response&);
void handle_scene_end(const httplib::Request&, httplib::Response&);
```

- [ ] **Step 2: Build to verify declaration compiles**

```bash
cd build && cmake --build . --target merak_http 2>&1 | tail -5
```

Expected: may produce "unresolved external" linker error for `handle_get_chapter` (expected — we haven't written the definition yet).

- [ ] **Step 3: Commit**

```bash
git add libs/http/include/merak/worldbuilding_http_handler.hpp
git commit -m "feat(http): declare handle_get_chapter handler"
```

---

### Task 7: Register GET route and implement handler

**Files:**
- Modify: `libs/http/src/worldbuilding_http_handler.cpp`

- [ ] **Step 1: Register route in `install_routes()`**

After the chapter list route registration (around line 207), insert the GET route:

```cpp
server.Get(R"(/api/worldbuilding/([^/]+)/chapters/([^/]+))",
    [this](const auto& req, auto& res) { handle_get_chapter(req, res); });
```

The chapter route block in `install_routes()` becomes:
```cpp
server.Get(R"(/api/worldbuilding/([^/]+)/chapters)",
    [this](const auto& req, auto& res) { handle_list_chapters(req, res); });
server.Get(R"(/api/worldbuilding/([^/]+)/chapters/([^/]+))",
    [this](const auto& req, auto& res) { handle_get_chapter(req, res); });
server.Patch(R"(/api/worldbuilding/([^/]+)/chapters/([^/]+))",
    [this](const auto& req, auto& res) { handle_patch_chapter(req, res); });
```

- [ ] **Step 2: Implement `handle_get_chapter`**

Add the implementation after `handle_list_chapters` (around line 688), before `handle_list_scenes`:

```cpp
void WorldbuildingHttpHandler::handle_get_chapter(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string cid = req.matches[2];

        if (!service_->worlds().get_world(wid)) {
            error_response(res, "World not found", 404, "world_not_found");
            return;
        }

        auto chapter = service_->narrative().get_chapter(wid, cid);
        if (!chapter) {
            error_response(res, "Chapter not found", 404, "chapter_not_found");
            return;
        }

        nlohmann::json response{
            {"ok", true},
            {"id", chapter->id},
            {"title", chapter->title},
            {"number", chapter->number},
            {"status", worldbuilding::to_string(chapter->status)},
            {"content", chapter->content},
            {"pitch", chapter->pitch},
            {"notes", chapter->notes},
            {"scene_ids", chapter->scene_ids},
            {"foreshadowing_planted", chapter->foreshadowing_planted},
            {"foreshadowing_paid", chapter->foreshadowing_paid}
        };
        response["arc_id"] = chapter->arc_id.has_value()
            ? nlohmann::json(*chapter->arc_id)
            : nlohmann::json(nullptr);

        json_response(res, response);
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}
```

- [ ] **Step 3: Build**

```bash
cd build && cmake --build . --target merak_http 2>&1 | tail -5
```

Expected: success.

- [ ] **Step 4: Commit**

```bash
git add libs/http/src/worldbuilding_http_handler.cpp
git commit -m "feat(http): implement GET /api/worldbuilding/:wid/chapters/:cid"
```

---

### Task 8: Full build verification

- [ ] **Step 1: Clean rebuild**

```bash
cd build && cmake --build . 2>&1 | tail -10
```

Expected: zero errors.

- [ ] **Step 2: Commit (if needed)**

Only if there are any remaining unstaged changes.

---

### Task 9: Database migration

**Files:**
- Modify: `libs/worldbuilding/src/narrative_store.cpp:240-269` (initialize method)

项目在 `NarrativeStore::initialize()` 中用 `CREATE TABLE IF NOT EXISTS` 管理 schema（行 240-269），无独立 migration 系统。需同时处理新安装和已有数据库升级。

- [ ] **Step 1: Add `content` column to CREATE TABLE**

在 `initialize()` 的 chapters CREATE TABLE 语句（行 249-253）中加入 `content` 列：

```cpp
conn.exec("CREATE TABLE IF NOT EXISTS chapters("
          "id TEXT PRIMARY KEY, world_id TEXT NOT NULL, arc_id TEXT,"
          "name TEXT, pitch TEXT, content TEXT DEFAULT '', status TEXT, position INT DEFAULT 0,"
          "created_at TIMESTAMPTZ DEFAULT now(),"
          "updated_at TIMESTAMPTZ DEFAULT now())");
```

- [ ] **Step 2: Add ALTER TABLE for existing databases**

在 chapters CREATE TABLE 之后、sections CREATE TABLE 之前加入幂等迁移：

```cpp
// 迁移：为已有数据库新增 content 列
try {
    conn.exec("ALTER TABLE chapters ADD COLUMN content TEXT DEFAULT ''");
} catch (const std::exception&) {
    // Column already exists — ignore
}
```

> 用 try-catch 而非 `IF NOT EXISTS`（并非所有 PostgreSQL 版本都支持 ALTER TABLE ADD COLUMN IF NOT EXISTS）。

- [ ] **Step 3: Build**

```bash
cd build && cmake --build . --target merak_worldbuilding 2>&1 | tail -5
```

Expected: success.

- [ ] **Step 4: Commit**

```bash
git add libs/worldbuilding/src/narrative_store.cpp
git commit -m "feat(worldbuilding): add content column migration for chapters table"
```

---

### Task 10: End-to-end verification

- [ ] **Step 1: Start the server**

```bash
cd build && ./merak 2>&1 &
```

Wait for server to start.

- [ ] **Step 2: Test GET on non-existent chapter**

```bash
curl -s http://127.0.0.1:PORT/api/worldbuilding/test_world/chapters/nonexistent | jq .
```

Expected: `{"ok":false,"error":{"code":"chapter_not_found","message":"Chapter not found","retryable":false}}`

- [ ] **Step 3: Test PATCH with content**

```bash
curl -s -X PATCH http://127.0.0.1:PORT/api/worldbuilding/WORLD_ID/chapters/CHAPTER_ID \
  -H 'Content-Type: application/json' \
  -d '{"fields":{"content":"# Test content"}}' | jq .
```

Expected: `{"ok":true}`

- [ ] **Step 4: Test GET reads back content**

```bash
curl -s http://127.0.0.1:PORT/api/worldbuilding/WORLD_ID/chapters/CHAPTER_ID | jq .content
```

Expected: `"# Test content"`

- [ ] **Step 5: Test PATCH with title + content**

```bash
curl -s -X PATCH http://127.0.0.1:PORT/api/worldbuilding/WORLD_ID/chapters/CHAPTER_ID \
  -H 'Content-Type: application/json' \
  -d '{"fields":{"title":"New Title","content":"Updated content"}}' | jq .
```

Expected: `{"ok":true}`

- [ ] **Step 6: Verify both fields persisted**

```bash
curl -s http://127.0.0.1:PORT/api/worldbuilding/WORLD_ID/chapters/CHAPTER_ID | jq '{title, content}'
```

Expected: `{"title":"New Title","content":"Updated content"}`
