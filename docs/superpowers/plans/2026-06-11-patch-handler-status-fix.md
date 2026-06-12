# PATCH Handler Status Validation Fix — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix hardcoded status validation in `handle_patch_scene` and `handle_patch_chapter` by adding `parse_*` functions to the enum definitions and adding existence checking to `handle_patch_chapter`.

**Architecture:** Add `parse_scene_status` and `parse_chapter_status` as `std::optional`-returning parse functions in `world_models.hpp`, co-located with their `to_string` counterparts. Then replace hardcoded string comparisons in both HTTP handlers with calls to these parse functions, and add a `get_chapter()` existence check to `handle_patch_chapter`.

**Tech Stack:** C++17, `std::optional`, `std::string_view`, httplib

---

### Task 1: Add `parse_chapter_status` to world_models.hpp

**Files:**
- Modify: `libs/worldbuilding/include/merak/worldbuilding/world_models.hpp:304`

- [ ] **Step 1: Add `parse_chapter_status` after `to_string(ChapterStatus)`**

Insert after line 304 (the closing `}` of `to_string(ChapterStatus)`):

```cpp

inline std::optional<ChapterStatus> parse_chapter_status(std::string_view s) {
    if (s == "outline") return ChapterStatus::Outline;
    if (s == "drafting") return ChapterStatus::Drafting;
    if (s == "completed") return ChapterStatus::Completed;
    if (s == "revised") return ChapterStatus::Revised;
    return std::nullopt;
}
```

- [ ] **Step 2: Commit**

```bash
git add libs/worldbuilding/include/merak/worldbuilding/world_models.hpp
git commit -m "feat(worldbuilding): add parse_chapter_status function"
```

---

### Task 2: Add `parse_scene_status` to world_models.hpp

**Files:**
- Modify: `libs/worldbuilding/include/merak/worldbuilding/world_models.hpp:316`

- [ ] **Step 1: Add `parse_scene_status` after `to_string(SceneStatus)`**

Insert after line 316 (the closing `}` of `to_string(SceneStatus)`):

```cpp

inline std::optional<SceneStatus> parse_scene_status(std::string_view s) {
    if (s == "draft") return SceneStatus::Draft;
    if (s == "writing") return SceneStatus::Writing;
    if (s == "completed") return SceneStatus::Completed;
    return std::nullopt;
}
```

- [ ] **Step 2: Commit**

```bash
git add libs/worldbuilding/include/merak/worldbuilding/world_models.hpp
git commit -m "feat(worldbuilding): add parse_scene_status function"
```

---

### Task 3: Fix `handle_patch_scene` status validation

**Files:**
- Modify: `libs/http/src/worldbuilding_http_handler.cpp:1085-1091`

- [ ] **Step 1: Replace hardcoded check with `parse_scene_status`**

Replace lines 1085-1091:

Before:
```cpp
        if (fields.contains("status")) {
            std::string s = fields["status"].get<std::string>();
            if (s != "drafting" && s != "writing" && s != "completed" && s != "archived") {
                error_response(res, "Invalid scene status: " + s, 400);
                return;
            }
        }
```

After:
```cpp
        if (fields.contains("status")) {
            auto parsed = worldbuilding::parse_scene_status(fields["status"].get<std::string>());
            if (!parsed) {
                error_response(res, "Invalid scene status", 400);
                return;
            }
        }
```

- [ ] **Step 2: Commit**

```bash
git add libs/http/src/worldbuilding_http_handler.cpp
git commit -m "fix(http): use parse_scene_status in handle_patch_scene"
```

---

### Task 4: Fix `handle_patch_chapter` — add existence check + status validation

**Files:**
- Modify: `libs/http/src/worldbuilding_http_handler.cpp:1111-1125`

- [ ] **Step 1: Add `get_chapter()` existence check and replace status validation**

Replace lines 1114-1125:

Before:
```cpp
    try {
        auto body = nlohmann::json::parse(req.body);
        auto fields = body.at("fields");
        if (fields.contains("status")) {
            std::string s = fields["status"].get<std::string>();
            if (s != "drafting" && s != "writing" && s != "completed" && s != "archived") {
                error_response(res, "Invalid chapter status: " + s, 400);
                return;
            }
        }
        service_->narrative().patch_chapter(wid, cid, fields);
        json_response(res, {{"ok", true}});
```

After:
```cpp
    try {
        auto chapter = service_->narrative().get_chapter(wid, cid);
        if (!chapter) {
            error_response(res, "Chapter not found", 404, "chapter_not_found");
            return;
        }
        auto body = nlohmann::json::parse(req.body);
        auto fields = body.at("fields");
        if (fields.contains("status")) {
            auto parsed = worldbuilding::parse_chapter_status(fields["status"].get<std::string>());
            if (!parsed) {
                error_response(res, "Invalid chapter status", 400);
                return;
            }
        }
        service_->narrative().patch_chapter(wid, cid, fields);
        json_response(res, {{"ok", true}});
```

- [ ] **Step 2: Commit**

```bash
git add libs/http/src/worldbuilding_http_handler.cpp
git commit -m "fix(http): add existence check and parse_chapter_status to handle_patch_chapter"
```
