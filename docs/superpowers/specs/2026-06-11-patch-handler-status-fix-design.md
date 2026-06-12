# PATCH Handler Status Validation Fix — Design Spec

**Date:** 2026-06-11
**Status:** approved

## Problem

Code review of the chapter content API implementation discovered two critical issues in `handle_patch_chapter`, both rooted in copy-paste from `handle_patch_scene` without adapting to the chapter domain:

1. **Return value ignored** — `patch_chapter()` returns `bool` (false when chapter doesn't exist), but the handler discards it, returning `{"ok":true}` for nonexistent chapters.
2. **Wrong enum validation** — Status validation uses SceneStatus values (`drafting/writing/completed/archived`) instead of ChapterStatus values (`outline/drafting/completed/revised`).

Additionally, `handle_patch_scene` itself has a status validation bug: it accepts `"drafting"` (should be `"draft"`) and `"archived"` (not a SceneStatus value at all).

## Root Cause

Hardcoded status strings in each handler, with no connection to the enum definitions in `world_models.hpp`. When `handle_patch_chapter` was created by copying `handle_patch_scene`, the status strings weren't updated.

## Design

### Principle: Single source of truth for enum values

The `to_string` functions in `world_models.hpp` define the canonical string representation for each enum. Add symmetric `parse_*` functions so both directions live in the same file. Handlers call the parse function — they never hardcode enum values.

### Changes

#### 1. `world_models.hpp` — Add parse functions

Add `parse_scene_status` and `parse_chapter_status` alongside their `to_string` counterparts:

```cpp
inline std::optional<SceneStatus> parse_scene_status(std::string_view s) {
    if (s == "draft") return SceneStatus::Draft;
    if (s == "writing") return SceneStatus::Writing;
    if (s == "completed") return SceneStatus::Completed;
    return std::nullopt;
}

inline std::optional<ChapterStatus> parse_chapter_status(std::string_view s) {
    if (s == "outline") return ChapterStatus::Outline;
    if (s == "drafting") return ChapterStatus::Drafting;
    if (s == "completed") return ChapterStatus::Completed;
    if (s == "revised") return ChapterStatus::Revised;
    return std::nullopt;
}
```

When a new enum value is added, the developer adds one line in `to_string` and one line in `parse_*` in the same file. All handlers pick it up automatically.

#### 2. `handle_patch_scene` — Replace hardcoded validation

Before:
```cpp
if (s != "drafting" && s != "writing" && s != "completed" && s != "archived") {
    error_response(res, "Invalid scene status: " + s, 400);
    return;
}
```

After:
```cpp
auto parsed = worldbuilding::parse_scene_status(fields["status"].get<std::string>());
if (!parsed) {
    error_response(res, "Invalid scene status", 400);
    return;
}
```

#### 3. `handle_patch_chapter` — Add existence check + replace validation

Before:
```cpp
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

Now mirrors `handle_patch_scene` exactly: get → validate → patch → respond.

## Error Handling

| Scenario | Status | Code |
|---|---|---|
| Chapter not found | 404 | `chapter_not_found` |
| Scene not found | 404 | `scene_not_found` |
| Invalid status value | 400 | `invalid_request` |
| Version conflict | 409 | `version_conflict` |
| Malformed body / other | 400 | `invalid_request` |

## What This Does NOT Change

- `patch_chapter()` and `patch_scene()` store functions — their signatures and behavior are correct
- Other PATCH handlers (`handle_patch_foreshadow`, `handle_patch_secret`) — they use a different pattern (check bool return from patch) but work correctly
- Frontend — no changes needed

## Files Touched

| File | Change |
|---|---|
| `libs/worldbuilding/include/merak/worldbuilding/world_models.hpp` | Add `parse_scene_status`, `parse_chapter_status` |
| `libs/http/src/worldbuilding_http_handler.cpp` | Fix `handle_patch_scene` status validation, fix `handle_patch_chapter` existence check + status validation |
