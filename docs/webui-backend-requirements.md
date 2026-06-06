# Merak WebUI Backend Requirements

This document defines the backend surface needed by the upgraded WebUI workbench. The
frontend currently uses typed fallback data when these endpoints are missing, so each
endpoint can be implemented incrementally without breaking the UI.

## Contract Defaults

- Runtime APIs stay under `/v1/*`; worldbuilding and workspace APIs stay under `/api/*`.
- All JSON responses use `ok: boolean`. Error responses should use:

```json
{
  "error": {
    "code": "string_code",
    "message": "Human readable message",
    "retryable": false
  }
}
```

- Timestamps are ISO 8601 strings.
- File content is UTF-8 for v1.
- IDs are stable strings and safe for use as React keys.

## Capability Discovery

### `GET /api/webui/capabilities`

Declares which workbench features are backed by real server behavior.

Response:

```json
{
  "ok": true,
  "capabilities": {
    "files": true,
    "story_overview": true,
    "session_archive": true,
    "world_create": true,
    "editor_save": true
  }
}
```

## Worldbuilding

### `GET /api/worldbuilding/worlds/{world_id}`

Response:

```json
{
  "ok": true,
  "world": {
    "id": "world_123",
    "name": "Northreach",
    "description": "A borderland under pressure.",
    "created_at": "2026-06-06T12:00:00Z",
    "updated_at": "2026-06-06T12:20:00Z",
    "stats": {
      "chapters": 4,
      "scenes": 18,
      "agents": 7,
      "foreshadowing_open": 5,
      "secrets_active": 3
    }
  }
}
```

### `POST /api/worldbuilding/worlds`

Request:

```json
{
  "name": "Northreach",
  "description": "A borderland under pressure."
}
```

Response:

```json
{
  "ok": true,
  "world": {
    "id": "world_123",
    "name": "Northreach",
    "description": "A borderland under pressure.",
    "created_at": "2026-06-06T12:00:00Z"
  }
}
```

The existing `{ ok, world_id, name }` shape may continue temporarily, but the WebUI
should eventually receive the full `world` object.

### `GET /api/worldbuilding/{world_id}/overview`

Query:

- `session_id` optional, narrows the current chapter/scene to a session.

Response:

```json
{
  "ok": true,
  "current_arc": {
    "id": "arc_1",
    "title": "Siege of Northreach",
    "status": "drafting",
    "purpose": "Push the protagonist into public leadership."
  },
  "current_chapter": {
    "id": "chapter_1",
    "title": "Snow at the Gate",
    "number": 1,
    "status": "drafting",
    "arc_id": "arc_1",
    "scene_count": 3,
    "updated_at": "2026-06-06T12:00:00Z"
  },
  "current_scene": {
    "id": "scene_1",
    "title": "The Inn Warning",
    "chapter_id": "chapter_1",
    "world_time": "Day 1 Dawn",
    "status": "writing",
    "participant_ids": ["agent_lina"],
    "updated_at": "2026-06-06T12:00:00Z"
  },
  "agents": [
    {
      "id": "agent_lina",
      "name": "lina",
      "display_name": "Lina",
      "kind": "individual"
    }
  ],
  "foreshadowing": [
    {
      "id": "fs_1",
      "content": "The broken seal appears before the trial.",
      "pay_off_idea": "Reveal it as proof of forged orders.",
      "status": "open",
      "hint_level": "visible",
      "tags": ["politics"],
      "planted_at": "scene_1",
      "paid_at": null
    }
  ],
  "secrets": [
    {
      "id": "secret_1",
      "title": "Hidden heir",
      "truth": "Lina is the heir.",
      "public_version": "Lina is a courier.",
      "stakes": "Civil conflict if exposed early.",
      "status": "active",
      "aware_character_ids": ["agent_god"],
      "suspicious_character_ids": []
    }
  ],
  "world_time": "Day 1 Dawn"
}
```

### `GET /api/worldbuilding/{world_id}/chapters`

Query:

- `status` optional.

Response:

```json
{
  "ok": true,
  "chapters": [
    {
      "id": "chapter_1",
      "title": "Snow at the Gate",
      "number": 1,
      "status": "drafting",
      "arc_id": "arc_1",
      "scene_count": 3,
      "updated_at": "2026-06-06T12:00:00Z"
    }
  ]
}
```

### `GET /api/worldbuilding/{world_id}/scenes`

Query:

- `chapter_id` optional.
- `status` optional.

Response:

```json
{
  "ok": true,
  "scenes": [
    {
      "id": "scene_1",
      "title": "The Inn Warning",
      "chapter_id": "chapter_1",
      "world_time": "Day 1 Dawn",
      "status": "writing",
      "participant_ids": ["agent_lina"],
      "updated_at": "2026-06-06T12:00:00Z"
    }
  ]
}
```

### `GET /api/worldbuilding/{world_id}/foreshadowing`

Query:

- `status` optional: `open`, `paid`, `abandoned`.

Response:

```json
{
  "ok": true,
  "items": [
    {
      "id": "fs_1",
      "content": "The broken seal appears before the trial.",
      "pay_off_idea": "Reveal it as proof of forged orders.",
      "status": "open",
      "hint_level": "visible",
      "tags": ["politics"],
      "planted_at": "scene_1",
      "paid_at": null
    }
  ]
}
```

### `GET /api/worldbuilding/{world_id}/secrets`

Query:

- `status` optional: `active`, `exposed`, `abandoned`.

Response:

```json
{
  "ok": true,
  "items": [
    {
      "id": "secret_1",
      "title": "Hidden heir",
      "truth": "Lina is the heir.",
      "public_version": "Lina is a courier.",
      "stakes": "Civil conflict if exposed early.",
      "status": "active",
      "aware_character_ids": ["agent_god"],
      "suspicious_character_ids": []
    }
  ]
}
```

## Workspace Files

### `GET /api/workspace/files`

Query:

- `session_id` optional.
- `world_id` optional.
- `root` optional absolute or Merak-home-relative root.
- `q` optional search term.
- `type` optional extension filter without dot, for example `md`.

Response:

```json
{
  "ok": true,
  "root": "C:/Users/example/.merak/worlds/world_123/outputs",
  "files": [
    {
      "id": "file_abc",
      "path": "C:/Users/example/.merak/worlds/world_123/outputs/chapter-01.md",
      "name": "chapter-01.md",
      "ext": "md",
      "mime": "text/markdown",
      "size": 2480,
      "updated_at": "2026-06-06T12:00:00Z",
      "generated_by_run_id": "run_123",
      "dirty": false
    }
  ]
}
```

### `GET /api/workspace/files/content`

Query:

- `path` required.

Response:

```json
{
  "ok": true,
  "file": {
    "path": "C:/Users/example/.merak/worlds/world_123/outputs/chapter-01.md",
    "content": "# Chapter 1\n\nDraft...",
    "encoding": "utf-8",
    "updated_at": "2026-06-06T12:00:00Z",
    "version": "mtime:1780747200:size:2480"
  }
}
```

### `PUT /api/workspace/files/content`

Request:

```json
{
  "path": "C:/Users/example/.merak/worlds/world_123/outputs/chapter-01.md",
  "content": "# Chapter 1\n\nRevised draft...",
  "version": "mtime:1780747200:size:2480"
}
```

Response:

```json
{
  "ok": true,
  "file": {
    "path": "C:/Users/example/.merak/worlds/world_123/outputs/chapter-01.md",
    "updated_at": "2026-06-06T12:03:00Z",
    "version": "mtime:1780747380:size:2600"
  }
}
```

Conflict response:

```json
{
  "error": {
    "code": "file_conflict",
    "message": "File changed on disk. Reload before saving.",
    "retryable": true
  }
}
```

## Sessions And Runs

### `POST /v1/sessions/{session_id}/archive`

Request:

```json
{
  "archived": true
}
```

Response:

```json
{
  "ok": true,
  "session": {
    "id": "session_123",
    "title": "Chapter planning",
    "last_seq": 42,
    "created_at": "2026-06-06T12:00:00Z",
    "updated_at": "2026-06-06T12:05:00Z",
    "archived_at": "2026-06-06T12:05:00Z"
  }
}
```

### `GET /v1/runs/{run_id}`

Response:

```json
{
  "ok": true,
  "run": {
    "id": "run_123",
    "session_id": "session_123",
    "status": "responding",
    "model": "claude-sonnet-4-6",
    "started_at": "2026-06-06T12:00:00Z",
    "completed_at": null,
    "input_tokens": 1200,
    "output_tokens": 640,
    "tool_calls": [
      {
        "id": "tool_1",
        "name": "write_file",
        "status": "completed",
        "started_at": "2026-06-06T12:01:00Z",
        "completed_at": "2026-06-06T12:01:02Z"
      }
    ]
  }
}
```

## SSE Event Additions

### `workspace_file_created`

```json
{
  "path": "C:/Users/example/.merak/worlds/world_123/outputs/chapter-01.md",
  "name": "chapter-01.md",
  "ext": "md",
  "size": 2480,
  "run_id": "run_123",
  "session_id": "session_123",
  "world_id": "world_123"
}
```

### `workspace_file_updated`

```json
{
  "path": "C:/Users/example/.merak/worlds/world_123/outputs/chapter-01.md",
  "version": "mtime:1780747380:size:2600",
  "updated_at": "2026-06-06T12:03:00Z",
  "run_id": "run_123"
}
```

### `story_context_updated`

```json
{
  "world_id": "world_123",
  "resource_type": "scene",
  "resource_id": "scene_1"
}
```

`resource_type` must be one of `scene`, `chapter`, `foreshadowing`, `secret`, or `agent`.

### `run_step_changed`

```json
{
  "run_id": "run_123",
  "step": "acting",
  "label": "Writing output file",
  "detail": "chapter-01.md"
}
```

`step` must be one of `thinking`, `acting`, `observing`, `responding`, or
`waiting_approval`.

## Implementation Priority

1. `/api/webui/capabilities`
2. Workspace file list/read/save
3. Story overview
4. Foreshadowing and secrets list implementations
5. Session archive and run detail
6. New SSE events for file and story updates
