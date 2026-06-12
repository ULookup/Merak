# Chapter Content API — Design Spec

> 2026-06-11 | `GET /api/worldbuilding/:wid/chapters/:cid` + `PATCH` content 字段修复

## 背景

前端 `ChapterEditor.tsx` 是一个所见即所得的章节编辑器，支持 `@agent{name=...}`、`@foreshadow{id=...}`、`@secret{id=...}` 标签语法。它调用两个接口：

| 接口 | 用途 | 当前状态 |
|------|------|----------|
| `GET /api/worldbuilding/:wid/chapters/:cid` | 加载章节内容 | 未注册路由，返回 404 |
| `PATCH /api/worldbuilding/:wid/chapters/:cid` | 保存章节（`{ title, content }`） | 路由存在，但 `content` 字段被静默丢弃 |

根本原因：`Chapter` 模型没有 `content` 字段。前端期望的"章节正文"没有对应的持久化载体。

## 设计目标

1. `Chapter` 模型新增 `content` 字段，独立于 `pitch`（梗概）和 `notes`（内部笔记）
2. 实现 `GET /api/worldbuilding/:wid/chapters/:cid`，返回完整章节对象
3. 修复 `PATCH` 使 `content` 字段正确持久化（JSON 文件 + PostgreSQL）
4. 前端无需改动——响应中 `content` 字段已满足现有消费

## 领域模型

```
Chapter
├── pitch          — 一句话梗概（概览、列表展示）
├── notes          — 作者内部工作笔记（规划、待办、灵感）
├── content        — 【新增】章节正文（ChapterEditor 编辑对象）
├── scenes[]       — 场景级叙述（Scene.narrative），粒度更细
```

`content` 和 `notes` 分离的原因：notes 是规划工作区，content 是产出物。同一字段复用会互相污染。

## 数据层

### Chapter 结构体变更

`libs/worldbuilding/include/merak/worldbuilding/world_models.hpp`:

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

### JSON 序列化

`libs/worldbuilding/src/narrative_store.cpp` — `chapter_json()` 和 `chapter_from_json()` 各加入 `content` 字段：

```cpp
// chapter_json: 写入
{"content", chapter.content}

// chapter_from_json: 读取
chapter.content = json.value("content", "");  // 用 value() 而非 at()，兼容历史数据
```

### PostgreSQL Schema

```sql
ALTER TABLE chapters ADD COLUMN content TEXT DEFAULT '';
```

### NarrativeStore

**新增方法**：

```cpp
// narrative_store.hpp
std::optional<Chapter> get_chapter(const std::string& world_id,
                                   const std::string& chapter_id) const;
```

实现：读 `{data_root}/worlds/{world_id}/chapters/{chapter_id}.json` → `chapter_from_json()`。

**`create_chapter` 变更**：

INSERT 语句加入 `content` 列：

```sql
INSERT INTO chapters(id, world_id, arc_id, name, pitch, content, status, position)
VALUES($1, $2, $3, $4, $5, $6, $7, $8)
```

**`patch_chapter` 变更**：

JSON 更新：
```cpp
if (fields.contains("content")) json["content"] = fields["content"];
```

DB UPDATE 的 SET 子句：
```cpp
if (fields.contains("content")) {
    set_parts.push_back("content = $" + std::to_string(param_idx++));
    params.push_back(fields["content"].get<std::string>());
}
```

## API 层

### `GET /api/worldbuilding/:wid/chapters/:cid`

**路由注册**（`worldbuilding_http_handler.cpp:install_routes`）：

```cpp
server.Get(R"(/api/worldbuilding/([^/]+)/chapters/([^/]+))",
    [this](const auto& req, auto& res) { handle_get_chapter(req, res); });
```

**Handler**：`WorldbuildingHttpHandler::handle_get_chapter`

1. 提取 `wid = req.matches[1]`, `cid = req.matches[2]`
2. 验证 world 存在 → 否则 404 `world_not_found`
3. 调用 `NarrativeStore::get_chapter(wid, cid)` → 否则 404 `chapter_not_found`
4. 返回完整 Chapter JSON

**响应 `200`**：

```json
{
  "ok": true,
  "id": "chapter_xxx",
  "title": "第一章",
  "number": 1,
  "status": "drafting",
  "content": "# 正文...\n@agent{name=张三} ...",
  "pitch": "章节梗概",
  "notes": "工作笔记",
  "arc_id": "arc_xxx",
  "scene_ids": ["scene_1"],
  "foreshadowing_planted": [],
  "foreshadowing_paid": []
}
```

**错误响应**：

| 场景 | 状态码 | 响应 |
|------|--------|------|
| 世界不存在 | 404 | `{"ok":false,"error":{"code":"world_not_found","message":"World not found","retryable":false}}` |
| 章节不存在 | 404 | `{"ok":false,"error":{"code":"chapter_not_found","message":"Chapter not found","retryable":false}}` |

**设计决策**：返回完整 Chapter 对象而非仅 `{ ok, content }`，因为前端已定义 `fetchChapterContent` 取 `data.content`，多出的字段被忽略，不影响现有行为。后续可利用 `pitch`、`notes`、`foreshadowing_planted` 等增强编辑器面板。

### `PATCH /api/worldbuilding/:wid/chapters/:cid`

HTTP handler 层无需改动——`handle_patch_chapter` 已通过 `body.at("fields")` 透传所有字段给 `NarrativeStore::patch_chapter()`。数据层加入 `content` 处理后，前端 `patchChapter(worldId, chapterId, { title, content })` 自动生效。

## 前端

无需改动。`ChapterEditor.tsx` 现有代码：

```typescript
// 加载 — data.content 来自新接口，直接可用
api.fetchChapterContent(worldId, chapterId)
  .then(data => setContent(data.content ?? ''));

// 保存 — content 现在会被后端正确持久化
api.patchChapter(worldId, chapterId, { title, content });
```

## 变更文件清单

| 文件 | 变更 |
|------|------|
| `libs/worldbuilding/include/merak/worldbuilding/world_models.hpp` | `Chapter` 新增 `content` 字段 |
| `libs/worldbuilding/include/merak/worldbuilding/narrative_store.hpp` | 新增 `get_chapter()` 声明 |
| `libs/worldbuilding/src/narrative_store.cpp` | `chapter_json`/`chapter_from_json` 加 content；`create_chapter` SQL 加 content；`patch_chapter` 处理 content；实现 `get_chapter()` |
| `libs/worldbuilding/src/narrative_service.cpp` | 新增 `get_chapter()` 委托 |
| `libs/http/include/merak/worldbuilding_http_handler.hpp` | 新增 `handle_get_chapter()` 声明 |
| `libs/http/src/worldbuilding_http_handler.cpp` | 注册 GET 路由 + 实现 handler |

## 测试

| 层级 | 测试点 |
|------|--------|
| NarrativeStore | `get_chapter` 存在时返回完整 Chapter；不存在时返回 nullopt |
| NarrativeStore | `create_chapter` 含 content → JSON 文件和 DB 均包含 |
| NarrativeStore | `patch_chapter({content: "new"})` → JSON 和 DB 均更新 |
| HTTP | `GET .../chapters/:cid` 存在 → 200 + 完整 JSON |
| HTTP | `GET .../chapters/:cid` 不存在 → 404 `chapter_not_found` |
| HTTP | `PATCH {content:"x"}` → `GET` 读到相同值（端到端） |
| 前端 | ChapterEditor 加载已有章节 → content 回填编辑器 |
| 前端 | ChapterEditor 保存 → 刷新后 content 不丢失 |
