# Merak WebUI API Reference

> 基于 `main` 分支源码严格生成，2026-06-16。
> 所有接口通过 `httplib::Server` 注册在 `127.0.0.1`，返回 JSON（SSE 端点返回 `text/event-stream`）。

---

## 通用约定

### 错误响应格式

**v1 接口**（`/v1/*`）返回：

```json
{
  "error": {
    "code": "session_not_found",
    "message": "Session does not exist",
    "retryable": false
  }
}
```

**worldbuilding 接口**（`/api/worldbuilding/*`）返回：

```json
{
  "ok": false,
  "error": {
    "code": "world_not_found",
    "message": "World not found",
    "retryable": false
  }
}
```

> **已知不一致**：`PATCH /v1/sessions/:id` 在 404 时返回 `{"error": "session not found"}` 而非标准 v1 错误格式。`PATCH /api/worldbuilding/worlds/:id` 在 404 时返回 `{"error": "..."}` 而非标准 worldbuilding 错误格式。Pipeline error 返回的是 `{"error":"..."}` 而非含 code 的结构。

### 常见错误码

| 错误码 | HTTP 状态 | 说明 |
|--------|----------|------|
| `session_not_found` | 404 | 会话不存在 |
| `session_busy` | 409 | 会话有未完成的 run |
| `run_not_found` | 404 | Run 不存在 |
| `approval_not_found` | 404 | 审批不存在 |
| `invalid_request` | 400 | 请求格式错误 |
| `invalid_path` | 400/403 | 路径非法或不在允许范围内 |
| `file_not_found` | 404 | 文件不存在 |
| `file_conflict` | 409 | 文件版本冲突（可重试） |
| `version_conflict` | 409 | 资源版本冲突（可重试） |
| `unsupported_file_type` | 415 | 不支持的文件类型 |
| `world_not_found` | 404 | 世界不存在 |
| `agent_not_found` | 404 | Agent 不存在 |
| `scene_not_found` | 404 | 场景不存在 |
| `chapter_not_found` | 404 | 章节不存在 |
| `foreshadow_not_found` | 404 | 伏笔不存在 |
| `secret_not_found` | 404 | 秘密不存在 |
| `config_load_failed` | 500 | 配置加载失败 |
| `config_save_failed` | 400 | 配置保存失败 |
| `test_failed` | 502 | LLM 连接测试失败 |
| `test_unavailable` | 503 | LLM Provider 不可用 |
| `title_generation_failed` | 500 | 标题生成失败 |
| `missing_param` | 400 | 缺少必填查询参数 |
| `missing_chapter_ids` | 400 | 缺少或无效的章节 ID 列表 |
| `time_not_forward` | 400 | 新时间不晚于当前世界时间 |
| `database_error` | 500 | 数据库操作错误 |
| `workflow_not_found` | 400 | Pipeline workflow 不存在 |
| `image_service_not_available` | 503 | Image Service 未初始化 |
| `image_not_found` | 404 | 图片不存在 |
| `invalid_image_type` | 400 | 图片类型必须为 avatar 或 design |
| `invalid_style` | 400 | preferred_style 非法值 |
| `preferences_write_failed` | 500 | 偏好写入失败 |
| `preferences_save_failed` | 400 | 偏好保存失败 |
| `export_failed` | 500 | 导出失败 |
| `review_failed` | 500 | 章节评审失败 |

---

## 1. 核心运行时 API

### 1.1 `GET /v1/runtime` — 运行时元数据

WebUI 初始化时第一个调用的接口，获取服务器能力、可用模型、工具列表等。

**请求**：无参数

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `provider` | string | 当前 LLM 提供商 |
| `model` | string | 当前默认模型名 |
| `models` | array | 可用模型列表（为空时回退为单个默认项，`max_context_tokens`=128000） |
| `models[].name` | string | 模型名称 |
| `models[].provider` | string | 模型提供商 |
| `models[].max_context_tokens` | number | 最大上下文 token 数 |
| `permission_mode` | string | 权限模式 |
| `memory.enabled` | boolean | 记忆功能是否启用 |
| `worldbuilding.enabled` | boolean | 世界观功能是否启用 |
| `tui.theme` | string | TUI 配色主题名 |
| `tools` | array | 可用工具列表 |
| `tools[].name` | string | 工具名称 |
| `tools[].description` | string | 工具描述 |
| `tools[].source` | string | 工具来源 |
| `tools[].requires_confirmation` | boolean | 是否需要用户确认 |
| `mcp_servers` | array | MCP 服务器状态 |
| `mcp_servers[].name` | string | 服务器名称 |
| `mcp_servers[].alive` | boolean | 是否存活 |
| `agents` | array | 可用 Agent 列表 |
| `agents[].id` | string | Agent ID |
| `agents[].description` | string | Agent 描述 |
| `delegation_patterns` | string[] | 支持的委托模式：`"fan_out"`, `"sequential"`, `"pipeline"` |

---

### 1.2 `GET /api/webui/capabilities` — UI 功能开关

告知前端哪些功能模块可用，前端据此显示/隐藏对应 UI。

**请求**：无参数

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 始终为 true |
| `capabilities.files` | boolean | 工作区文件浏览功能 |
| `capabilities.story_overview` | boolean | 故事概览功能 |
| `capabilities.session_archive` | boolean | 会话归档功能 |
| `capabilities.world_create` | boolean | 世界创建功能 |
| `capabilities.editor_save` | boolean | 编辑器保存功能 |

---

### 1.3 `POST /v1/sessions` — 创建会话

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `title` | string | 否 | 会话标题，默认空字符串 |
| `world_id` | string | 否 | 关联的世界 ID |
| `agent_id` | string | 否 | 关联的 Agent ID |

**响应 `201`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `session_id` | string | 新创建的会话 ID |
| `session` | object | 会话详情 |
| `session.id` | string | 会话 ID |
| `session.title` | string | 会话标题 |
| `session.world_id` | string | 关联世界 ID（空字符串表示无关联） |
| `session.agent_id` | string | 关联 Agent ID（空字符串表示无关联） |
| `session.last_seq` | number | 最后事件序号 |
| `session.created_at` | string | 创建时间 (ISO 8601) |
| `session.updated_at` | string | 更新时间 (ISO 8601) |
| `session.archived_at` | string\|null | 归档时间，未归档为 null |

**`400`**：`invalid_request` — 请求体 JSON 解析失败

---

### 1.4 `GET /v1/sessions` — 列出所有会话

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `world_id` | string | 否 | 按世界 ID 过滤 |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `sessions` | array | 会话列表（按 updated_at 降序） |
| `sessions[].id` | string | 会话 ID |
| `sessions[].title` | string | 会话标题 |
| `sessions[].world_id` | string | 关联世界 ID |
| `sessions[].agent_id` | string | 关联 Agent ID |
| `sessions[].last_seq` | number | 最后事件序号 |
| `sessions[].created_at` | string | 创建时间 |
| `sessions[].updated_at` | string | 更新时间 |
| `sessions[].archived_at` | string\|null | 归档时间 |

---

### 1.5 `GET /v1/sessions/:id` — 获取单个会话

**路径参数**：`id` — 会话 ID

**响应 `200`**：同 session 对象（id, title, world_id, agent_id, last_seq, created_at, updated_at, archived_at）

**`404`**：`session_not_found` — 会话不存在

---

### 1.6 `PATCH /v1/sessions/:id` — 更新会话标题

**路径参数**：`id` — 会话 ID

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `title` | string | 是 | 新标题 |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `session` | object | 更新后的会话对象 |

**`404`**：会话不存在（返回 `{"error": "session not found"}`，非标准错误格式）

---

### 1.7 `POST /v1/sessions/:id/archive` — 归档/取消归档

**路径参数**：`id` — 会话 ID

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `archived` | boolean | 否 | true=归档，false=取消，默认 true |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `session` | object | 更新后的会话对象 |

**`400`**：`invalid_request` — 请求体 JSON 解析失败
**`404`**：`session_not_found` — 会话不存在

---

### 1.8 `POST /v1/sessions/:id/generate-title` — AI 生成标题

让 LLM 根据会话内容自动生成标题，不写入会话历史，不产生 run 记录。

**路径参数**：`id` — 会话 ID

**请求体**：无

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `title` | string | 生成的标题 |

**`500`**：`title_generation_failed`

---

### 1.9 `GET /v1/worlds/:wid/agents/:aid/session` — 获取或创建 Agent 会话

查找指定世界和 Agent 的活跃（未归档）会话，如果不存在则自动创建。

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `aid` | Agent ID |

**响应 `200`**（已存在活跃会话）：

| 字段 | 类型 | 说明 |
|------|------|------|
| `session` | object | 会话对象 |
| `created` | boolean | false（表示复用已有会话） |

**响应 `201`**（新建会话）：

| 字段 | 类型 | 说明 |
|------|------|------|
| `session` | object | 新创建的会话对象 |
| `created` | boolean | true |

---

### 1.10 `GET /v1/sessions/:id/events` — 获取会话事件（轮询）

**路径参数**：`id` — 会话 ID

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `after` | number | 否 | 事件序号游标，返回此序号之后的事件，默认 0 |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `events` | array | 事件列表 |

> **注意**：`message_appended` 和 `compaction_applied` 类型的事件不会出现在此列表中。消息历史使用 `/memory` 接口。

**`404`**：`session_not_found`

**事件对象通用结构**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `seq` | number | 事件序号 |
| `type` | string | 事件类型（详见 [SSE 事件类型](#sse-事件类型参考)） |
| `run_id` | string | 关联的 run ID |
| `timestamp` | string | 事件时间戳 (ISO 8601) |
| `session_id` | string | 会话 ID |
| `payload` | object | 事件负载（结构因 type 而异） |

---

### 1.11 `GET /v1/sessions/:id/memory` — 获取会话消息历史

从事件流中提取所有 `message_appended` 事件构建消息列表。

**路径参数**：`id` — 会话 ID

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `session_id` | string | 会话 ID |
| `items` | array | 消息列表 |
| `items[].index` | number | 消息序号（从 1 开始） |
| `items[].role` | string | 角色：`"user"` / `"assistant"` / `"tool"` / `"system"` |
| `items[].content` | string | 消息内容 |
| `items[].tool_call_id` | string | 关联的工具调用 ID（tool 消息时有值，其余为空字符串） |

**`404`**：`session_not_found`

---

### 1.12 `POST /v1/sessions/:id/runs` — 发送消息，启动 Agent 运行

消息将触发完整的 Agent 思考→工具调用→响应循环。同步返回 run_id，异步通过 SSE 推送结果。

**路径参数**：`id` — 会话 ID

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `message` | string | 是 | 用户输入消息 |
| `model` | string | 否 | 指定模型，空则使用默认模型 |

**响应 `202`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `run_id` | string | 新创建的 run ID |
| `session_id` | string | 会话 ID |
| `model` | string | 使用的模型名 |

**`400`**：`invalid_request` — 请求体 JSON 解析失败
**`409`**：`session_busy` — 会话已有未完成的 run

---

### 1.13 `POST /v1/sessions/:id/delegations` — 创建多 Agent 委托

**路径参数**：`id` — 会话 ID

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `pattern` | string | 否 | 委托模式：`"fan_out"`(默认) / `"sequential"` / `"pipeline"` |
| `task` | string | 否 | 委托任务描述，默认空字符串 |
| `agents` | string[] | 否 | 参与委托的 Agent ID 列表，默认空数组 |
| `aggregation` | string | 否 | 结果聚合策略，默认 `"all_results"` |

**响应 `202`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `delegation_id` | string | 委托 ID |
| `parent_run_id` | string | 父 run ID |
| `session_id` | string | 会话 ID |

**`400`**：`invalid_request` — 请求体 JSON 解析失败
**`404`**：`session_not_found` 或 `agent_not_found`
**`409`**：`session_busy`

---

### 1.14 `POST /v1/approvals/:id` — 处理审批请求

**路径参数**：`id` — 审批 ID

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `decision` | string | 是 | `"allow"` 或 `"deny"`（非 allow 均视为 deny） |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `approval_id` | string | 审批 ID |
| `status` | string | 处理后的状态：`"allowed"` / `"denied"` |

**`404`**：`approval_not_found`

---

### 1.15 `POST /v1/creations/:id/resolve` — 处理创建请求

用于用户对 Agent 的创建请求（如创建文件、角色等）做出决定，可附带修改。

**路径参数**：`id` — 创建请求 ID

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `decision` | string | 否 | `"allow"` / `"deny"` / `"modify"`，默认 `"deny"` |
| `modifications` | object | 否 | 对创建内容的修改建议，默认空对象（用于 modify 决定） |

**响应 `200`**：由 `RuntimeService::resolve_creation()` 返回，结构可能包含：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `creation_id` | string | 创建请求 ID |
| `decision` | string | 最终决定 |
| `scene_id` | string | 若创建的是场景 |
| `chapter_id` | string | 若创建的是章节 |
| `arc_id` | string | 若创建的是弧线 |

**`400`**：`invalid_request` — 请求体 JSON 解析失败
**`404`**：创建请求不存在

---

### 1.16 `POST /v1/runs/:id/cancel` — 取消运行

递归取消所有关联的子运行。

**路径参数**：`id` — run ID

**请求体**：无

**响应 `202`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `run_id` | string | run ID |
| `status` | string | `"cancelled"` |

**`404`**：run 不存在

---

### 1.17 `POST /v1/runs/:id/ask-response` — 回答 Agent 的提问

当 Agent 调用 `ask_user` 工具向用户发起交互式提问时，前端通过此接口提交用户的回答，唤醒阻塞的 Agent 循环。

**路径参数**：`id` — run ID

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `call_id` | string | 是 | 工具调用 ID（来自 SSE 事件 `ask_user_requested` 的 `call_id`） |
| `response` | string | 是 | 用户回答文本 |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 始终为 true |
| `run_id` | string | run ID |
| `call_id` | string | 工具调用 ID |

**`400`**：`invalid_request` — call_id 为空
**`404`**：`run_not_found`

> **前端状态**：后端已完整实现，SSE 事件 `ask_user_requested` 已推送，但 `webui/src/api/client.ts` 中尚未添加此接口的调用方法。

---

### 1.18 `GET /v1/runs/:id` — 获取运行详情

从事件流中聚合工具调用和 token 用量信息。

**路径参数**：`id` — run ID

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `run.id` | string | run ID |
| `run.session_id` | string | 所属会话 ID |
| `run.status` | string | `"queued"` / `"running"` / `"waiting_approval"` / `"completed"` / `"failed"` / `"cancelled"` / `"interrupted"` |
| `run.model` | string | 使用的模型（取自 metadata） |
| `run.started_at` | string | 开始时间 |
| `run.completed_at` | string\|null | 完成时间（未完成时为 null） |
| `run.input_tokens` | number | 输入 token 总数 |
| `run.output_tokens` | number | 输出 token 总数 |
| `run.tool_calls` | array | 工具调用记录 |
| `run.tool_calls[].id` | string | 调用 ID |
| `run.tool_calls[].name` | string | 工具名称 |
| `run.tool_calls[].arguments` | string | 调用参数 (JSON 字符串) |
| `run.tool_calls[].status` | string | `"running"` / `"completed"` / `"failed"` |
| `run.tool_calls[].started_at` | string | 开始时间 |
| `run.tool_calls[].completed_at` | string | 完成时间 |
| `run.tool_calls[].output` | string | 工具输出（完成/失败时有值） |
| `run.tool_calls[].is_error` | boolean | 是否为错误输出 |

**`404`**：`run_not_found` — Run 不存在

---

### 1.19 `GET /v1/sessions/:id/events/stream` — SSE 事件流

建立 Server-Sent Events 长连接，实时推送会话事件。

**路径参数**：`id` — 会话 ID

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `after` | number | 否 | 事件序号游标，先回放 backlog 再推送新事件 |

**Content-Type**：`text/event-stream`（chunked）

**帧格式**：
```
id: {seq}
event: {type}
data: {json_payload}

```

**连接行为**：
1. 先推送 `after` 之后的所有历史事件（过滤掉 `message_appended` 和 `compaction_applied`）
2. 阻塞等待新事件（最多 1 秒），有新事件则推送，无新事件则发送 `: keepalive` 心跳
3. 前端断线后自动重连（指数退避，初始 1s，最大 30s，最多 10 次）

**前端实现**：`webui/src/hooks/useSSE.ts` — `fetch()` + `ReadableStream`

**`404`**：session 不存在

#### SSE 事件类型

**运行生命周期**：

| event | payload 字段 | 说明 |
|-------|-------------|------|
| `run_started` | `message` | 用户消息已被接受 |
| `run_completed` | — | 正常运行结束 |
| `run_failed` | `error` | 运行异常终止 |
| `run_cancelled` | — | 被用户取消 |
| `run_interrupted` | `reason` | 服务重启导致中断 |

**Agent 状态与文本流**：

| event | payload 字段 | 说明 |
|-------|-------------|------|
| `state_changed` | `from`, `to` | Agent 状态切换（如 `thinking` → `acting`） |
| `run_step_changed` | `step` | 步骤标签变更（thinking/acting/waiting_approval） |
| `text_delta` | `text` | 增量文本输出（流式打字效果） |
| `usage_updated` | `input_tokens`, `output_tokens`, `exact` | Token 用量更新 |

**工具调用**：

| event | payload 字段 | 说明 |
|-------|-------------|------|
| `tool_started` | `id`, `name`, `arguments` | 工具调用开始 |
| `tool_completed` | `id`, `name`, `output`, `is_error` | 工具调用完成 |

**审批、创建与交互式提问**：

| event | payload 字段 | 说明 |
|-------|-------------|------|
| `approval_requested` | `approval_id`, `tool`, `arguments`, `tool_call_id` | 需要用户审批 |
| `approval_resolved` | `approval_id`, `decision` | 审批已处理 |
| `creation_requested` | `creation_id`, `tool`, `preview` | Agent 请求创建资源 |
| `creation_resolved` | `creation_id`, `tool`, `decision`, `result` | 创建请求已处理 |
| `ask_user_requested` | `call_id`, `question`, `options`, `multi_select` | Agent 向用户发起交互式提问 |

**委托与子运行**：

| event | payload 字段 | 说明 |
|-------|-------------|------|
| `delegation_started` | `delegation_id`, `pattern`, `agent_ids`, `task` | 委托启动 |
| `delegation_completed` | `delegation_id`, `status`, `aggregated_output`, `input_tokens`, `output_tokens` | 委托完成 |
| `sub_run_started` | `run_id`, `parent_run_id`, `delegation_id`, `agent_id`, `task` | 子运行开始 |
| `sub_run_completed` | `run_id`, `delegation_id`, `agent_id`, `status`, `output_preview`, `input_tokens`, `output_tokens` | 子运行结束 |
| `sub_run_state_changed` | `from`, `to` | 子运行状态切换 |
| `sub_run_text_delta` | `text` | 子运行增量文本 |
| `sub_run_tool_started` | `id`, `name`, `arguments` | 子运行工具调用开始 |
| `sub_run_tool_completed` | `id`, `name`, `output`, `is_error` | 子运行工具调用完成 |
| `sub_run_usage_updated` | `input_tokens`, `output_tokens` | 子运行 token 用量 |

**会话与工作区**：

| event | payload 字段 | 说明 |
|-------|-------------|------|
| `session_created` | `title` | 会话创建 |
| `session_updated` | `title` | 会话标题已更新 |
| `workspace_file_created` | `path`, `version`, `updated_at` | 工具创建了新文件 |
| `workspace_file_updated` | `path`, `version`, `updated_at`, `run_id` | 工作区文件被保存 |
| `story_context_updated` | `world_id`, `resource_type`, `resource_id` | 世界观资源变更 |
| `scene_changed` | — | 活跃场景变更 |

**Pipeline**：

| event | payload 字段 | 说明 |
|-------|-------------|------|
| `pipeline_phase_changed` | `from_phase`, `to_phase` | Pipeline 阶段切换 |
| `pipeline_condition_progress` | `conditions[]` | 条件评估进度 |
| `pipeline_condition_met` | — | 所有 advance 条件已满足 |
| `pipeline_advance_failed` | `reason` | 阶段推进失败 |
| `pipeline_cycle_complete` | — | 完整 Pipeline 循环结束 |
| `pipeline_action` | 由 workflow 定义 | 通用 pipeline SSE 事件 |

**内部事件（不在 SSE 流中推送）**：

| event | 说明 |
|-------|------|
| `message_appended` | 消息写入历史（用 `/memory` 接口获取） |
| `compaction_applied` | 上下文压缩 |

---

## 2. 配置 API

### 2.1 `GET /api/config/llm` — 获取 LLM 配置

从服务器缓存的配置中返回。

**请求**：无参数

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `provider` | string | LLM 提供商 |
| `api_key_masked` | string | 脱敏后的 API Key（如 `"********sk-abc"`，短于 4 字符显示 `"****"`） |
| `api_base_url` | string | API 基础 URL |
| `default_model` | string | 默认模型 |
| `max_output_tokens` | number | 最大输出 token 数 |
| `temperature` | number | 温度参数 |
| `context_memory_length` | number | 上下文记忆长度 |

**`500`**：`config_load_failed` — 缓存配置为 null

---

### 2.2 `POST /api/config/llm` — 保存 LLM 配置

写入 `{merak_home}/settings.local.json`，与已有配置合并（不会覆盖未传入的字段）。空字符串视为不修改。

**请求体**（所有字段可选）：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `provider` | string | 否 | LLM 提供商 |
| `api_key` | string | 否 | API Key（空字符串不覆盖） |
| `api_base_url` | string | 否 | API 基础 URL |
| `default_model` | string | 否 | 默认模型 |
| `max_output_tokens` | number | 否 | 最大输出 token 数 |
| `temperature` | number | 否 | 温度参数 |
| `context_memory_length` | number | 否 | 上下文记忆长度 |
| `writer_model` | string | 否 | 写作模型（写入 `memory.writer_model`） |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 始终为 true |
| `restart_required` | boolean | 始终为 true，提示需要重启生效 |

**`400`**：`config_save_failed` — 保存过程中异常

---

### 2.3 `POST /api/config/llm/test` — 测试 LLM 连接

用当前配置发起轻量测试调用。

**请求**：无参数 / 无 body

**响应 `200`**：
```json
{"ok": true, "test": "passed"}
```

**`502`**：`test_failed` — LLM 连接测试失败
**`503`**：`test_unavailable` — LLM Provider 未初始化

---

### 2.4 `GET /api/config/preferences` — 获取用户偏好

从 `{merak_home}/preferences.json` 读取。

**请求**：无参数

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 始终为 true |
| `default_genre` | string | 默认创作类型 |
| `preferred_style` | string | 偏好的写作风格 |
| `allow_usage_logs` | boolean | 是否允许使用统计 |

---

### 2.5 `PUT /api/config/preferences` — 更新用户偏好

写入 `{merak_home}/preferences.json`。

**请求体**（所有字段可选）：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `default_genre` | string | 否 | 默认创作类型 |
| `preferred_style` | string | 否 | 偏好的写作风格，必须是 `"轻松"` / `"严肃"` / `"诗意"` / `"简洁"` |
| `allow_usage_logs` | boolean | 否 | 是否允许使用统计 |

**响应 `200`**：
```json
{"ok": true}
```

**`400`**：`invalid_style` — preferred_style 不合法；`preferences_save_failed` — 保存异常
**`500`**：`preferences_write_failed` — 写入文件失败

---

## 3. 工作区文件 API

### 3.1 `GET /api/workspace/files` — 列出工作区文件

列出工作区目录下的文本文件（md/markdown/txt/json/yaml/yml），自动忽略隐藏文件、`.git` 目录和 `runtime.db`/`sessions.db`。

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `root` | string | 否 | 工作区根目录（优先级最高） |
| `world_id` | string | 否 | 世界 ID，自动定位到 `worlds/{id}/outputs` |
| `q` | string | 否 | 搜索关键词（大小写不敏感，匹配相对路径） |
| `type` | string | 否 | 文件类型：`"all"`(默认) / `"markdown"` / `"text"` / `"data"` |

> **路径解析优先级**：`root` > `world_id` > 默认 `{merak_home}/outputs`。所有路径必须在 merak home 下。

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `root` | string | 实际使用的工作区根目录 |
| `files` | array | 文件列表 |
| `files[].id` | string | 文件完整路径（同 path） |
| `files[].path` | string | 文件完整路径 |
| `files[].relative_path` | string | 相对 root 的路径 |
| `files[].name` | string | 文件名 |
| `files[].ext` | string | 扩展名（小写，不含点） |
| `files[].mime` | string | MIME 类型 |
| `files[].size` | number | 文件大小（字节） |
| `files[].updated_at` | string | 最后修改时间 (ISO 8601) |
| `files[].generated_by_run_id` | null | 预留字段 |
| `files[].dirty` | boolean | 预留字段，固定 false |

**`403`**：`invalid_path` — 工作区根目录不在 merak home 下
**`500`**：`workspace_list_failed` — 其他异常

---

### 3.2 `GET /api/workspace/files/content` — 读取文件内容

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `path` | string | 是 | 文件路径（相对路径基于 merak home 解析） |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `file.path` | string | 文件完整路径 |
| `file.content` | string | 文件内容 |
| `file.encoding` | string | 编码，固定为 `"utf-8"` |
| `file.updated_at` | string | 最后修改时间 |
| `file.version` | string | 版本标识（格式 `"mtime:{秒}:size:{字节}"`） |

**`400`**：`invalid_request` — 缺少 path 参数
**`403`**：`invalid_path` — 路径不在 merak home 下
**`404`**：`file_not_found` — 文件不存在
**`415`**：`unsupported_file_type` — 非文本文件
**`500`**：`file_read_failed` — 读取异常

---

### 3.3 `PUT /api/workspace/files/content` — 保存文件内容

支持乐观锁冲突检测。保存成功后通过 SSE 广播 `workspace_file_updated` 事件。

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `path` | string | 是 | 文件路径 |
| `content` | string | 是 | 文件内容 |
| `version` | string | 否 | 乐观锁版本号，不匹配则返回 409 |
| `session_id` | string | 否 | 关联会话（用于 SSE 广播） |
| `run_id` | string | 否 | 关联 run（用于 SSE 广播） |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `file.path` | string | 文件路径 |
| `file.updated_at` | string | 保存后的修改时间 |
| `file.version` | string | 保存后的版本号 |

**`400`**：`invalid_request` — 缺少 path 或 content，或 JSON 解析失败
**`403`**：`invalid_path` — 路径不在 merak home 下
**`409`**：`file_conflict` — 文件已被修改（可重试）
**`415`**：`unsupported_file_type` — 非文本文件
**`500`**：`file_save_failed` — 保存异常

---

### 3.4 `POST /api/workspace/open` — 在系统应用中打开文件

Linux 使用 `xdg-open`，macOS 使用 `open`，Windows 使用 `ShellExecuteW`。

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `path` | string | 是 | 要打开的文件或目录路径 |
| `reveal` | boolean | 否 | true=在文件管理器中选中，false=直接打开，默认 false |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `path` | string | 实际打开路径的规范化形式 |

**`400`**：`invalid_path` — 缺少 path
**`404`**：`path_not_found` — 路径不存在
**`500`**：`open_failed` — 打开失败或异常

---

## 4. 世界观 API

### 4.1 世界 CRUD

#### `GET /api/worldbuilding/worlds` — 列出所有世界

**请求**：无参数

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `worlds` | array | 世界列表 |
| `worlds[].id` | string | 世界 ID |
| `worlds[].name` | string | 世界名称 |
| `worlds[].description` | string | 世界描述 |
| `worlds[].created_at` | string | 创建时间 |
| `worlds[].updated_at` | string | 更新时间 |
| `worlds[].active_sessions` | number | 活跃会话数 |

**`400`**：`invalid_request` — 内部异常

---

#### `POST /api/worldbuilding/worlds` — 创建世界

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `name` | string | 是 | 世界名称 |
| `description` | string | 否 | 世界描述，默认空字符串 |

**响应 `201`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `world` | object | 世界详情（id, name, description, created_at, updated_at） |
| `world_id` | string | 世界 ID |
| `name` | string | 世界名称 |
| `description` | string | 世界描述 |

**`400`**：`invalid_request` — JSON 解析失败或内部异常

---

#### `GET /api/worldbuilding/worlds/:id` — 获取世界详情（含统计）

**路径参数**：`id` — 世界 ID

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `world` | object | 世界详情（含 id, name, description, created_at, updated_at） |
| `world.stats.agents` | number | Agent 数量 |
| `world.stats.chapters` | number | 章节数量 |
| `world.stats.scenes` | number | 场景数量 |
| `world.stats.open_foreshadowing` | number | 未回收伏笔数量 |
| `world.stats.active_secrets` | number | 活跃秘密数量 |

**`400`**：`invalid_request` — 内部异常
**`404`**：`world_not_found`

---

#### `DELETE /api/worldbuilding/worlds/:id` — 删除世界

**路径参数**：`id` — 世界 ID

**响应 `200`**：
```json
{"deleted": "<world_id>"}
```

---

#### `PATCH /api/worldbuilding/worlds/:id` — 更新世界

**路径参数**：`id` — 世界 ID

**请求体**（所有字段可选）：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `name` | string | 否 | 新名称 |
| `description` | string | 否 | 新描述 |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `world_id` | string | 世界 ID |
| `name` | string | 更新后的名称 |
| `description` | string | 更新后的描述 |

**`404`**：世界不存在（返回 `{"error": "..."}`，非标准 worldbuilding 错误格式）

---

### 4.2 角色 Agent

#### `GET /api/worldbuilding/:wid/agents` — 列出 Agent

**路径参数**：`wid` — 世界 ID

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `agents` | array | Agent 列表 |
| `agents[].id` | string | Agent ID |
| `agents[].name` | string | Agent 名称（内部标识） |
| `agents[].display_name` | string | 显示名称 |
| `agents[].kind` | string | 类型：`"individual"` / `"group"` / `"relation_manager"` 等 |
| `agents[].avatar_url` | string | 主头像 URL（有头像时存在，无头像时不返回此字段） |

**`400`**：`invalid_request` — 内部异常
**`404`**：`world_not_found`

---

#### `POST /api/worldbuilding/:wid/agents` — 创建角色

创建成功后通过 SSE 广播 `story_context_updated` 事件（若提供了 `session_id`）。

**路径参数**：`wid` — 世界 ID

**请求体**（CharacterCard）：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `name` | string | 是 | 角色名称（内部标识） |
| `gender` | string | 否 | 性别，默认空字符串 |
| `age` | number | 否 | 年龄，默认 0 |
| `race` | string | 否 | 种族，默认空字符串 |
| `identity` | string | 否 | 身份/职业，默认空字符串 |
| `emotional_tendency` | string | 否 | 情感倾向，默认空字符串 |
| `speaking_style` | string | 否 | 说话风格，默认空字符串 |
| `core_desire` | string | 否 | 核心欲望/动机，默认空字符串 |
| `deep_fear` | string | 否 | 深层恐惧，默认空字符串 |
| `daily_goal` | string | 否 | 日常目标，默认空字符串 |
| `background` | string | 否 | 背景故事，默认空字符串 |
| `knowledge_scope` | string | 否 | 知识范围，默认空字符串 |
| `appearance` | string | 否 | 外貌描述，默认空字符串 |
| `core_traits` | string[] | 否 | 核心特质，默认空数组 |
| `taboo_topics` | string[] | 否 | 禁忌话题，默认空数组 |
| `version` | number | 否 | 角色卡版本号，默认 1 |
| `session_id` | string | 否 | 关联会话（用于 SSE 广播） |

**响应 `201`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `agent_id` | string | 新创建的 Agent ID |
| `name` | string | Agent 名称 |

**`400`**：`invalid_request` — JSON 解析失败或内部异常

---

#### `GET /api/worldbuilding/:wid/agents/:aid` — 获取 Agent 详情（含角色卡）

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `aid` | Agent ID |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `agent` | object | Agent 详情 |
| `agent.id` | string | Agent ID |
| `agent.world_id` | string | 所属世界 ID |
| `agent.name` | string | 名称 |
| `agent.display_name` | string | 显示名称 |
| `agent.kind` | string | 类型 |
| `agent.created_at` | string | 创建时间 |
| `agent.updated_at` | string | 更新时间 |
| `agent.character_card` | object | 角色卡完整信息 |
| `agent.character_card.version` | number | 版本号 |
| `agent.character_card.age` | number | 年龄 |
| `agent.character_card.gender` | string | 性别 |
| `agent.character_card.race` | string | 种族 |
| `agent.character_card.identity` | string | 身份 |
| `agent.character_card.core_traits` | string[] | 核心特质 |
| `agent.character_card.emotional_tendency` | string | 情感倾向 |
| `agent.character_card.speaking_style` | string | 说话风格 |
| `agent.character_card.core_desire` | string | 核心欲望 |
| `agent.character_card.deep_fear` | string | 深层恐惧 |
| `agent.character_card.daily_goal` | string | 日常目标 |
| `agent.character_card.background` | string | 背景故事 |
| `agent.character_card.knowledge_scope` | string | 知识范围 |
| `agent.character_card.appearance` | string | 外貌描述 |
| `agent.character_card.taboo_topics` | string[] | 禁忌话题 |
| `agent.avatar_url` | string | 主头像 URL（有头像时存在，无头像时不返回此字段） |
| `agent.images` | object | 角色图片分组 |
| `agent.images.avatar` | array | 头像图片列表 |
| `agent.images.design` | array | 人设图列表 |

**`400`**：`invalid_request` — 内部异常
**`404`**：`agent_not_found` — Agent 不存在或不属于该世界

---

#### `PATCH /api/worldbuilding/:wid/agents/:aid` — 更新角色卡

支持乐观锁版本冲突检测。

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `aid` | Agent ID |

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `fields` | object | 是 | 要更新的字段（键为 character_card 中的字段名） |
| `version` | number | 否 | 乐观锁版本号，默认 0 |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `version` | number | 更新后的版本号 |

**`400`**：`invalid_request` — 缺少 fields 或内部异常
**`404`**：`agent_not_found` — Agent 不存在或不属于该世界
**`409`**：`version_conflict` — 版本冲突，返回：

```json
{
  "ok": false,
  "error": {
    "code": "version_conflict",
    "message": "资源已被其他来源修改，请刷新后重试",
    "current_version": <number>,
    "retryable": true
  }
}
```

---

#### `GET /api/worldbuilding/:wid/agents/:aid/diaries` — 获取角色日记

返回最近 50 条日记。

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `aid` | Agent ID |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `diaries` | array | 日记列表 |
| `diaries[].id` | string | 日记 ID |
| `diaries[].agent_id` | string | Agent ID |
| `diaries[].scene_id` | string | 关联场景 ID |
| `diaries[].content` | string | 日记内容 |
| `diaries[].world_time` | string | 世界时间 |
| `diaries[].created_at` | string | 创建时间 |

**`400`**：`invalid_request` — 内部异常
**`404`**：`agent_not_found` — Agent 不存在或不属于该世界

---

#### `POST /api/worldbuilding/:wid/agents/:aid/diaries` — 添加日记

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `aid` | Agent ID |

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `content` | string | 是 | 日记内容 |
| `scene_id` | string | 否 | 关联场景 ID，默认空字符串 |
| `world_time` | string | 否 | 世界时间，默认空字符串 |

**响应 `201`**：`{"ok": true}`

**`400`**：`invalid_request` — JSON 解析失败或缺少 content
**`404`**：`agent_not_found` — Agent 不存在或不属于该世界

---

#### `GET /api/worldbuilding/:wid/agents/:aid/relations` — 获取角色关系

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `aid` | Agent ID |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `relations` | array | 关系列表 |
| `relations[].agent_id` | string | 源角色 ID |
| `relations[].target_id` | string | 目标角色 ID |
| `relations[].relation_type` | string | 关系类型 |
| `relations[].description` | string | 关系描述 |
| `relations[].intimacy` | number | 亲密度 |
| `relations[].key_events` | string[] | 关键事件 |
| `relations[].updated_at` | string | 更新时间 |

**`400`**：`invalid_request` — 内部异常
**`404`**：`agent_not_found` — Agent 不存在或不属于该世界

---

#### `GET /api/worldbuilding/agents/:aid/prompt` — 加载角色 Prompt

用于 LLM 角色扮演的 system prompt。注意：此路径不含 world_id。

**路径参数**：`aid` — Agent ID

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `agent_id` | string | Agent ID |
| `prompt` | string | 生成的 prompt 文本 |

**`400`**：`invalid_request` — 内部异常

---

### 4.3 角色图片

角色图片分为两种类型：`avatar`（头像）和 `design`（人设图）。

#### `GET /api/worldbuilding/images/:iid` — 获取图片二进制

直接返回图片的二进制数据。注意：此路径不含 world_id 或 agent_id。

**路径参数**：`iid` — 图片 ID

**响应 `200`**：`Content-Type` 为图片的 MIME 类型，body 为原始二进制数据

**`404`**：`image_not_found`
**`503`**：Image Service 未初始化

---

#### `GET /api/worldbuilding/:wid/agents/:aid/images` — 列出角色图片

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `aid` | Agent ID |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `images` | array | 图片列表（按 sort_order 升序，created_at 降序） |
| `images[].id` | string | 图片 ID |
| `images[].agent_id` | string | 所属 Agent ID |
| `images[].image_type` | string | `"avatar"` 或 `"design"` |
| `images[].mime_type` | string | MIME 类型 |
| `images[].original_name` | string | 原始文件名 |
| `images[].file_size_bytes` | number | 文件大小（字节） |
| `images[].is_primary` | boolean | 是否为主头像 |
| `images[].sort_order` | number | 排序序号 |
| `images[].created_at` | string | 创建时间 (ISO 8601) |
| `images[].url` | string | 图片访问 URL（`/api/worldbuilding/images/{id}`） |

**`400`**：`invalid_request` — 内部异常
**`503`**：Image Service 未初始化

---

#### `POST /api/worldbuilding/:wid/agents/:aid/images` — 上传图片（简单模式）

适用于小于 10 MB 的文件。

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `aid` | Agent ID |

**请求**：`Content-Type: multipart/form-data`

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `image_type` | string | 是 | `"avatar"` 或 `"design"` |
| `file` | file | 是 | 图片文件（PNG / JPEG / WebP / GIF，最大 10 MB） |

**响应 `201`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `image` | object | 创建的图片记录（结构同列表中的 image 对象） |

**`400`**：`invalid_request` — 缺少必填字段或图片类型非法
**`503`**：Image Service 未初始化

---

#### `DELETE /api/worldbuilding/:wid/agents/:aid/images/:iid` — 删除图片

同时删除存储中的文件和数据库记录。

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `aid` | Agent ID |
| `iid` | 图片 ID |

**响应 `200`**：`{"ok": true}`

**`503`**：Image Service 未初始化

---

#### `PATCH /api/worldbuilding/:wid/agents/:aid/images/:iid` — 更新图片元数据

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `aid` | Agent ID |
| `iid` | 图片 ID |

**请求体**（可选字段，至少提供一个）：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `is_primary` | boolean | 否 | 设为 true 时自动取消同类型其他图片的 primary 状态 |
| `sort_order` | number | 否 | 排序序号 |

**响应 `200`**：`{"ok": true}`

**`400`**：图片不存在
**`503`**：Image Service 未初始化

---

#### `POST /api/worldbuilding/:wid/agents/:aid/images/chunked` — 初始化分块上传

支持断点续传。

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `aid` | Agent ID |

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `image_type` | string | 是 | `"avatar"` 或 `"design"` |
| `file_name` | string | 是 | 原始文件名 |
| `mime_type` | string | 是 | MIME 类型 |
| `total_size` | number | 是 | 文件总大小（字节） |
| `chunk_size` | number | 否 | 每块大小（字节），默认 5MB |

**响应 `201`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `upload_id` | string | 分块上传 ID |
| `chunks_total` | number | 总块数 |
| `chunk_size` | number | 每块大小 |

**`400`**：`invalid_request` — 缺少必填字段或图片类型非法
**`503`**：Image Service 未初始化

---

#### `PUT /api/worldbuilding/:wid/agents/:aid/images/chunked/:uid` — 上传分块

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `aid` | Agent ID |
| `uid` | 分块上传 ID |

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `chunk_idx` | number | 是 | 当前块索引（0-based） |

**请求**：`Content-Type: application/octet-stream`，body 为原始二进制数据

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `chunk_idx` | number | 已上传的块索引 |
| `uploaded_chunks` | number[] | 已上传的所有块索引列表 |

**`400`**：分块上传不存在或写入失败
**`503`**：Image Service 未初始化

---

#### `GET /api/worldbuilding/:wid/agents/:aid/images/chunked/:uid` — 查询分块上传进度

用于断点续传：客户端重连后查询已上传的块，从断点处继续。

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `aid` | Agent ID |
| `uid` | 分块上传 ID |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `uploaded_chunks` | number[] | 已上传的块索引列表 |

**`400`**：分块上传不存在
**`503`**：Image Service 未初始化

---

#### `POST /api/worldbuilding/:wid/agents/:aid/images/chunked/:uid/complete` — 完成分块上传

拼接所有块、校验大小、写入存储、创建数据库记录。

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `aid` | Agent ID |
| `uid` | 分块上传 ID |

**响应 `201`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `image` | object | 创建的图片记录 |

**`400`**：分块不完整、大小校验失败或上传不存在
**`503`**：Image Service 未初始化

---

#### `DELETE /api/worldbuilding/:wid/agents/:aid/images/chunked/:uid` — 取消分块上传

删除临时分块文件，释放资源。

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `aid` | Agent ID |
| `uid` | 分块上传 ID |

**响应 `200`**：`{"ok": true}`

**`503`**：Image Service 未初始化

---

### 4.4 叙事

#### `GET /api/worldbuilding/:wid/overview` — 故事概览

故事工作台的核心数据接口。

**路径参数**：`wid` — 世界 ID

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `current_arc` | object\|null | 当前故事弧线（id, title, purpose, status, updated_at） |
| `current_chapter` | object\|null | 当前章节 |
| `current_chapter.id` | string | 章节 ID |
| `current_chapter.title` | string | 章节标题 |
| `current_chapter.number` | number | 章节编号 |
| `current_chapter.status` | string | 状态：`"outline"` / `"drafting"` / `"completed"` / `"revised"` |
| `current_chapter.arc_id` | string\|null | 关联弧线 ID |
| `current_chapter.scene_count` | number | 场景数量 |
| `current_chapter.updated_at` | string | 更新时间 |
| `current_scene` | object\|null | 当前场景 |
| `current_scene.id` | string | 场景 ID |
| `current_scene.title` | string | 场景标题 |
| `current_scene.chapter_id` | string | 所属章节 ID |
| `current_scene.world_time` | string | 世界时间 |
| `current_scene.status` | string | 状态：`"draft"` / `"writing"` / `"completed"` |
| `current_scene.participant_ids` | string[] | 参与角色 ID |
| `current_scene.updated_at` | string | 更新时间 |
| `agents` | array | 所有 Agent 列表 |
| `foreshadowing` | array | 开放伏笔列表 |
| `foreshadowing[].id` | string | 伏笔 ID |
| `foreshadowing[].content` | string | 伏笔内容 |
| `foreshadowing[].pay_off_idea` | string | 回收思路 |
| `foreshadowing[].status` | string | `"open"` / `"paid"` / `"abandoned"` |
| `foreshadowing[].hint_level` | string | `"visible"` / `"subtle"` / `"obvious"` |
| `foreshadowing[].tags` | string[] | 标签 |
| `foreshadowing[].planted_at` | string\|null | 埋设时间 |
| `foreshadowing[].paid_at` | string\|null | 回收时间 |
| `secrets` | array | 活跃秘密列表 |
| `secrets[].id` | string | 秘密 ID |
| `secrets[].title` | string | 秘密标题 |
| `secrets[].truth` | string | 真相 |
| `secrets[].public_version` | string | 公开版本 |
| `secrets[].stakes` | string | 利害关系 |
| `secrets[].status` | string | `"active"` / `"exposed"` / `"abandoned"` |
| `secrets[].aware_character_ids` | string[] | 知情的角色 ID |
| `secrets[].suspicious_character_ids` | string[] | 怀疑的角色 ID |
| `world_time` | object | 世界时间 |
| `world_time.day` | number | 天数，固定 1 |
| `world_time.period` | number | 时段（0=晨），固定 0 |
| `world_time.label` | string | 时间标签，固定 `"第一日晨"` |

**`400`**：`invalid_request` — 内部异常
**`404`**：`world_not_found`

---

#### `GET /api/worldbuilding/:wid/chapters` — 列出章节

**路径参数**：`wid` — 世界 ID

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `status` | string | 否 | 过滤：`"outline"` / `"drafting"` / `"completed"` / `"revised"` |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `chapters` | array | 章节列表（含 id, title, number, status, scene_count, updated_at, arc_id） |

**`400`**：`invalid_request` — 非法 status 参数或内部异常

---

#### `GET /api/worldbuilding/:wid/chapters/:cid` — 获取章节详情

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `cid` | 章节 ID |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `id` | string | 章节 ID |
| `title` | string | 章节标题 |
| `number` | number | 章节编号 |
| `status` | string | 状态：`"outline"` / `"drafting"` / `"completed"` / `"revised"` |
| `content` | string | 章节正文 |
| `pitch` | string | 章节梗概 |
| `notes` | string | 写作笔记 |
| `scene_ids` | string[] | 关联场景 ID 列表 |
| `foreshadowing_planted` | string[] | 本章埋设的伏笔 ID |
| `foreshadowing_paid` | string[] | 本章回收的伏笔 ID |
| `arc_id` | string\|null | 关联弧线 ID |

**`404`**：`world_not_found` 或 `chapter_not_found`

---

#### `PATCH /api/worldbuilding/:wid/chapters/:cid` — 更新章节

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `cid` | 章节 ID |

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `fields` | object | 是 | 要更新的字段。status 必须为 `"outline"` / `"drafting"` / `"completed"` / `"revised"` |

**响应 `200`**：`{"ok": true}`

**`400`**：`invalid_request` — 缺少 fields、非法 status 值或内部异常
**`409`**：`version_conflict` — 版本冲突

---

#### `GET /api/worldbuilding/:wid/chapters/:cid/review` — 章节写作评审

返回章节的量化分析和写作建议。

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `cid` | 章节 ID |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `review.chapter_id` | string | 章节 ID |
| `review.title` | string | 章节标题 |
| `review.word_count` | number | 字数统计 |
| `review.character_names` | string[] | 出场的角色名列表 |
| `review.foreshadowing_planted` | array | 本章埋设的伏笔（含 `id`, `content`） |
| `review.foreshadowing_paid` | array | 本章回收的伏笔（含 `id`, `content`） |
| `review.writing_advice` | string | 写作建议 |

**`500`**：`review_failed`

---

#### `GET /api/worldbuilding/:wid/scenes` — 列出场景

**路径参数**：`wid` — 世界 ID

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `chapter_id` | string | 否 | 按章节过滤 |
| `status` | string | 否 | 按状态过滤：`"draft"` / `"writing"` / `"completed"` |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `scenes` | array | 场景列表（含 id, title, chapter_id, world_time, status, participant_ids, updated_at） |

**`400`**：`invalid_request` — 非法 status 参数或内部异常

---

#### `POST /api/worldbuilding/:wid/scenes` — 创建场景

创建成功后通过 SSE 广播 `story_context_updated` 事件（若提供了 `session_id`）。初始状态固定为 `draft`。

**路径参数**：`wid` — 世界 ID

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `title` | string | 否 | 场景标题（也可用 `name`，优先取 `title`） |
| `chapter_id` | string | 否 | 所属章节 ID，默认空字符串 |
| `world_time` | string | 否 | 世界时间，默认空字符串 |
| `narrative` | string | 否 | 叙述内容，默认空字符串 |
| `section_id` | string\|null | 否 | 关联区域 ID |
| `location_id` | string\|null | 否 | 关联地点 ID |
| `participant_ids` | string[] | 否 | 参与角色 ID 列表，默认空数组 |
| `session_id` | string | 否 | 关联会话（用于 SSE 广播） |

**响应 `201`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `scene_id` | string | 新创建的场景 ID |

**`400`**：`invalid_request` — JSON 解析失败或内部异常

---

#### `PATCH /api/worldbuilding/:wid/scenes/:sid` — 更新场景

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `sid` | 场景 ID |

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `fields` | object | 是 | 要更新的字段。status 必须为 `"draft"` / `"writing"` / `"completed"` |

**响应 `200`**：`{"ok": true}`

**`400`**：`invalid_request` — 缺少 fields、非法 status 值或内部异常
**`404`**：`scene_not_found`
**`409`**：`version_conflict` — 版本冲突

---

#### `POST /api/worldbuilding/:wid/scenes/:sid/end` — 结束场景

触发日记撰写、关系更新和伏笔建议。结束后通过 SSE 广播 `story_context_updated` 事件（若提供了 `session_id`）。

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `sid` | 场景 ID |

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `final_markdown` | string | 否 | 场景最终文本，默认空字符串 |
| `session_id` | string | 否 | 关联会话（用于 SSE 广播） |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `scene_id` | string | 场景 ID |
| `status` | string | 固定为 `"completed"` |
| `world_time` | string | 世界时间 |
| `relations_updated` | number | 更新的关系数量 |
| `foreshadowing_proposed` | array | 建议的伏笔（含 `id`, `content`） |
| `pending_diary_agents` | string[] | 待撰写日记的角色 ID 列表 |
| `pending_diary_count` | number | 待撰写日记数量 |

**`400`**：`invalid_request` — JSON 解析失败或内部异常

---

### 4.5 时间

#### `GET /api/worldbuilding/:wid/time` — 获取世界时间

当前返回固定值（Day 1, Dawn）。

**路径参数**：`wid` — 世界 ID

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `day` | number | 天数，固定 1 |
| `period` | number | 时段（0=晨, 1=昼, 2=午, 3=晚, 4=夜），固定 0 |
| `label` | string | 时间标签（UTF-8 编码中文，`"第一日晨"`） |

**`404`**：`world_not_found`

---

#### `POST /api/worldbuilding/:wid/time/advance` — 推进世界时间

只允许向前推进（新时间必须严格晚于当前世界时间）。

**路径参数**：`wid` — 世界 ID

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `world_time` | string | 是 | 新世界时间（如 `"第2日晨"`、`"2h"`、`"1d"`、`"day2_dawn"`） |
| `description` | string | 否 | 时间推进描述，默认 `"Time advanced"` |
| `recorded_by` | string | 否 | 记录者，默认 `"user"` |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `world_id` | string | 世界 ID |
| `world_time` | string | 推进后的世界时间 |
| `event_id` | string | 时间变更事件 ID |
| `description` | string | 时间推进描述 |

**`400`**：`time_not_forward` — 新时间不晚于当前时间

---

### 4.6 伏笔

#### `GET /api/worldbuilding/:wid/foreshadowing` — 列出伏笔

**路径参数**：`wid` — 世界 ID

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `status` | string | 否 | 过滤：`"open"` / `"paid"` / `"abandoned"` |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `items` | array | 伏笔列表 |
| `foreshadowing` | array | 同上（冗余字段，前端兼容） |

**`400`**：`invalid_request` — 非法 status 参数或内部异常

---

#### `POST /api/worldbuilding/:wid/foreshadowing` — 埋设伏笔

创建成功后通过 SSE 广播 `story_context_updated` 事件（若提供了 `session_id`）。

**路径参数**：`wid` — 世界 ID

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `content` | string | 是 | 伏笔内容（也可用 `hint`，优先取 `content`） |
| `pay_off_idea` | string | 否 | 回收思路，默认空字符串 |
| `hint_level` | string | 否 | 暗示级别：`"visible"`(默认) / `"subtle"` / `"obvious"`，非法值当作 visible |
| `tags` | string[] | 否 | 标签，默认空数组 |
| `session_id` | string | 否 | 关联会话（用于 SSE 广播） |

**响应 `201`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `item` | object | 创建的伏笔详情 |
| `foreshadowing_id` | string | 伏笔 ID |

**`400`**：`invalid_request` — JSON 解析失败或内部异常

---

#### `PATCH /api/worldbuilding/:wid/foreshadowing/:id` — 更新伏笔

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `id` | 伏笔 ID |

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `fields` | object | 是 | 要更新的字段。status 必须为 `"open"` / `"paid"` / `"abandoned"` |

**响应 `200`**：`{"ok": true}`

**`400`**：`invalid_request` — 缺少 fields、非法 status 值或内部异常
**`404`**：`foreshadow_not_found`
**`409`**：`version_conflict` — 版本冲突

---

### 4.7 秘密

#### `GET /api/worldbuilding/:wid/secrets` — 列出秘密

**路径参数**：`wid` — 世界 ID

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `status` | string | 否 | 过滤：`"active"` / `"exposed"` / `"abandoned"` |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `items` | array | 秘密列表 |
| `secrets` | array | 同上（冗余字段，前端兼容） |

**`400`**：`invalid_request` — 非法 status 参数或内部异常

---

#### `POST /api/worldbuilding/:wid/secrets` — 创建秘密

创建成功后通过 SSE 广播 `story_context_updated` 事件（若提供了 `session_id`）。

**路径参数**：`wid` — 世界 ID

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `holder_id` | string | 否 | 秘密持有者（也可用 `title`，优先取 `holder_id`），默认空字符串 |
| `truth` | string | 否 | 真相，默认空字符串 |
| `public_version` | string | 否 | 公开版本，默认空字符串 |
| `stakes` | string | 否 | 利害关系，默认空字符串 |
| `aware_character_ids` | string[] | 否 | 知情角色 ID，默认空数组 |
| `suspicious_character_ids` | string[] | 否 | 怀疑角色 ID，默认空数组 |
| `related_foreshadowing_ids` | string[] | 否 | 关联伏笔 ID，默认空数组 |
| `session_id` | string | 否 | 关联会话（用于 SSE 广播） |

**响应 `201`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `item` | object | 创建的秘密详情 |
| `secret_id` | string | 秘密 ID |

**`400`**：`invalid_request` — JSON 解析失败或内部异常

---

#### `PATCH /api/worldbuilding/:wid/secrets/:id` — 更新秘密

**路径参数**：

| 参数 | 说明 |
|------|------|
| `wid` | 世界 ID |
| `id` | 秘密 ID |

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `fields` | object | 是 | 要更新的字段。status 必须为 `"active"` / `"revealed"` / `"abandoned"` |

**响应 `200`**：`{"ok": true}`

**`400`**：`invalid_request` — 缺少 fields、非法 status 值或内部异常
**`404`**：`secret_not_found`
**`409`**：`version_conflict` — 版本冲突

---

### 4.8 导出

#### `POST /api/worldbuilding/:wid/export` — 导出章节

将指定章节拼接导出为单个 Markdown 文件。

**路径参数**：`wid` — 世界 ID

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `chapter_ids` | string[] | 是 | 要导出的章节 ID 列表，不能为空 |
| `title` | string | 否 | 导出文档标题，默认空字符串 |
| `author` | string | 否 | 作者名，默认空字符串 |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `file_path` | string | 导出文件的绝对路径 |
| `total_chars` | number | 导出文件的总字符数 |

**`400`**：`missing_chapter_ids` — 缺少或无效的 chapter_ids
**`500`**：`export_failed`

---

### 4.9 Pipeline

#### `GET /api/worldbuilding/:wid/pipeline/state` — 获取 Pipeline 状态

**路径参数**：`wid` — 世界 ID

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `phase` | string | 当前阶段 |
| `label` | string | 当前阶段的人类可读标签 |
| `active_workflow` | string | 激活的 workflow 名称 |
| `conditions` | array | 当前条件列表 |
| `conditions[].name` | string | 条件名称/描述 |
| `conditions[].met` | boolean | 是否满足 |
| `conditions[].current` | number\|null | 当前值（可选） |
| `conditions[].target` | number\|null | 目标值（可选） |
| `all_conditions_met` | boolean | 所有条件是否满足 |
| `next_allowed` | string[] | 允许推进到的下一阶段列表 |
| `allowed_retreat` | string[] | 允许回退到的阶段列表 |
| `recent_history` | array | 最近的阶段变更历史 |
| `recent_history[].id` | string | 变更记录 ID |
| `recent_history[].from` | string | 来源阶段 |
| `recent_history[].to` | string | 目标阶段 |
| `recent_history[].trigger` | string | 触发方式 |
| `recent_history[].timestamp` | string | 时间戳 |

**`503`**：Pipeline Manager 未初始化（返回 `{"error":"pipeline_not_available"}`，非标准格式）

---

#### `POST /api/worldbuilding/:wid/pipeline/advance` — 推进 Pipeline 阶段

**路径参数**：`wid` — 世界 ID

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `target_phase` | string | 否 | 目标阶段（不指定则由 Pipeline 自动选择） |
| `force` | boolean | 否 | 是否强制执行，默认 false |

**响应 `200`**：
```json
{"ok": true}
```

**`400`**：推进失败，返回：
```json
{"ok": false, "error": "<失败原因字符串>"}
```

**`503`**：Pipeline Manager 未初始化

---

#### `GET /api/worldbuilding/pipeline/workflows` — 列出可用 Workflows

**请求**：无参数

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `workflows` | array | Workflow 列表 |
| `workflows[].name` | string | Workflow 名称 |
| `workflows[].description` | string | 描述 |
| `workflows[].version` | string | 版本号 |
| `workflows[].phase_count` | number | 阶段数量 |

**`503`**：Pipeline Manager 未初始化

---

#### `GET /api/worldbuilding/pipeline/history` — 查询 Pipeline 历史

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `world_id` | string | 是 | 世界 ID |
| `limit` | number | 否 | 返回条数，1-100，默认 10 |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `history` | array | 变更历史列表 |
| `history[].id` | string | 记录 ID |
| `history[].world_id` | string | 世界 ID |
| `history[].from_phase` | string | 来源阶段 |
| `history[].to_phase` | string | 目标阶段 |
| `history[].trigger` | string | 触发方式 |
| `history[].triggered_by` | string\|null | 触发者 |
| `history[].conditions_summary` | object | 转换时的条件状态 |
| `history[].conditions_summary.all_met` | boolean | 所有条件是否满足 |
| `history[].conditions_summary.phase` | string | 阶段标识 |
| `history[].conditions_summary.results` | array | 各条件结果（name, met, current?, target?） |
| `history[].timestamp` | string | 时间戳 |

**`400`**：`missing_param` — 缺少 world_id；`invalid_request` — limit 参数非法
**`500`**：`database_error` — 数据库错误
**`503`**：Pipeline Manager 未初始化

---

#### `POST /api/worldbuilding/:wid/pipeline/activate` — 激活 Workflow

**路径参数**：`wid` — 世界 ID

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `workflow_name` | string | 否 | Workflow 名称，默认 `"default_creative_pipeline"` |

**响应 `200`**：
```json
{"ok": true}
```

**`400`**：`workflow_not_found` — 指定的 workflow 不存在
**`503`**：Pipeline Manager 未初始化

---

## 附录 A：乐观锁与版本控制

以下接口支持乐观锁，通过 `version` 字段防止并发写入冲突：

- `PATCH /api/worldbuilding/:wid/agents/:aid` — 角色卡更新
- `PATCH /api/worldbuilding/:wid/chapters/:cid` — 章节更新
- `PATCH /api/worldbuilding/:wid/scenes/:sid` — 场景更新
- `PATCH /api/worldbuilding/:wid/foreshadowing/:id` — 伏笔更新
- `PATCH /api/worldbuilding/:wid/secrets/:id` — 秘密更新

冲突时返回 409，响应体包含 `error.code: "version_conflict"` 和 `error.current_version` 供前端刷新。

文件保存（`PUT /api/workspace/files/content`）使用 `version` 字段（格式 `"mtime:{秒}:size:{字节}"`）实现类似的乐观锁检测，冲突时返回 `"file_conflict"`。

---

## 附录 B：枚举值汇总

| 枚举 | 可选值 |
|------|--------|
| RunStatus | `queued`, `running`, `waiting_approval`, `completed`, `failed`, `cancelled`, `interrupted` |
| ChapterStatus | `outline`, `drafting`, `completed`, `revised` |
| SceneStatus | `draft`, `writing`, `completed` |
| ForeshadowStatus | `open`, `paid`, `abandoned` |
| ForeshadowHintLevel | `visible`, `subtle`, `obvious` |
| SecretStatus（列表/创建） | `active`, `exposed`, `abandoned` |
| SecretStatus（PATCH） | `active`, `revealed`, `abandoned` |
| DelegationPattern | `fan_out`, `sequential`, `pipeline` |
| ApprovalDecision | `allow`, `deny` |
| CreationDecision | `allow`, `deny`, `modify` |
| AgentKind | `individual`, `group`, `relation_manager` |
| TimePeriod | 0=晨, 1=昼, 2=午, 3=晚, 4=夜 |
| PreferredStyle | `轻松`, `严肃`, `诗意`, `简洁` |

---

## 附录 C：端点完整性清单

### 核心运行时（28 个）

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/v1/runtime` | 运行时元数据 |
| GET | `/api/webui/capabilities` | UI 功能开关 |
| POST | `/v1/sessions` | 创建会话 |
| GET | `/v1/sessions` | 列出会话 |
| GET | `/v1/sessions/:id` | 获取会话 |
| PATCH | `/v1/sessions/:id` | 更新会话标题 |
| POST | `/v1/sessions/:id/archive` | 归档/取消归档 |
| POST | `/v1/sessions/:id/generate-title` | AI 生成标题 |
| GET | `/v1/worlds/:wid/agents/:aid/session` | 获取或创建 Agent 会话 |
| GET | `/v1/sessions/:id/events` | 获取会话事件（轮询） |
| GET | `/v1/sessions/:id/memory` | 获取消息历史 |
| POST | `/v1/sessions/:id/runs` | 发送消息启动运行 |
| POST | `/v1/sessions/:id/delegations` | 创建多 Agent 委托 |
| POST | `/v1/approvals/:id` | 处理审批请求 |
| POST | `/v1/creations/:id/resolve` | 处理创建请求 |
| POST | `/v1/runs/:id/cancel` | 取消运行 |
| POST | `/v1/runs/:id/ask-response` | 回答 Agent 提问 |
| GET | `/v1/runs/:id` | 获取运行详情 |
| GET | `/v1/sessions/:id/events/stream` | SSE 事件流 |
| GET | `/api/config/llm` | 获取 LLM 配置 |
| POST | `/api/config/llm` | 保存 LLM 配置 |
| POST | `/api/config/llm/test` | 测试 LLM 连接 |
| GET | `/api/config/preferences` | 获取用户偏好 |
| PUT | `/api/config/preferences` | 更新用户偏好 |
| GET | `/api/workspace/files` | 列出工作区文件 |
| GET | `/api/workspace/files/content` | 读取文件内容 |
| PUT | `/api/workspace/files/content` | 保存文件内容 |
| POST | `/api/workspace/open` | 打开文件/目录 |

### 世界观（46 个）

| 方法 | 路径 | 分类 |
|------|------|------|
| GET | `/api/worldbuilding/worlds` | 世界 |
| POST | `/api/worldbuilding/worlds` | 世界 |
| GET | `/api/worldbuilding/worlds/:id` | 世界 |
| DELETE | `/api/worldbuilding/worlds/:id` | 世界 |
| PATCH | `/api/worldbuilding/worlds/:id` | 世界 |
| GET | `/api/worldbuilding/:wid/agents` | Agent |
| POST | `/api/worldbuilding/:wid/agents` | Agent |
| GET | `/api/worldbuilding/:wid/agents/:aid` | Agent |
| PATCH | `/api/worldbuilding/:wid/agents/:aid` | Agent |
| GET | `/api/worldbuilding/:wid/agents/:aid/diaries` | Agent |
| POST | `/api/worldbuilding/:wid/agents/:aid/diaries` | Agent |
| GET | `/api/worldbuilding/:wid/agents/:aid/relations` | Agent |
| GET | `/api/worldbuilding/agents/:aid/prompt` | Agent |
| GET | `/api/worldbuilding/images/:iid` | 图片 |
| GET | `/api/worldbuilding/:wid/agents/:aid/images` | 图片 |
| POST | `/api/worldbuilding/:wid/agents/:aid/images` | 图片 |
| DELETE | `/api/worldbuilding/:wid/agents/:aid/images/:iid` | 图片 |
| PATCH | `/api/worldbuilding/:wid/agents/:aid/images/:iid` | 图片 |
| POST | `/api/worldbuilding/:wid/agents/:aid/images/chunked` | 图片 |
| PUT | `/api/worldbuilding/:wid/agents/:aid/images/chunked/:uid` | 图片 |
| GET | `/api/worldbuilding/:wid/agents/:aid/images/chunked/:uid` | 图片 |
| POST | `/api/worldbuilding/:wid/agents/:aid/images/chunked/:uid/complete` | 图片 |
| DELETE | `/api/worldbuilding/:wid/agents/:aid/images/chunked/:uid` | 图片 |
| GET | `/api/worldbuilding/:wid/overview` | 叙事 |
| GET | `/api/worldbuilding/:wid/chapters` | 叙事 |
| GET | `/api/worldbuilding/:wid/chapters/:cid` | 叙事 |
| PATCH | `/api/worldbuilding/:wid/chapters/:cid` | 叙事 |
| GET | `/api/worldbuilding/:wid/chapters/:cid/review` | 叙事 |
| GET | `/api/worldbuilding/:wid/scenes` | 叙事 |
| POST | `/api/worldbuilding/:wid/scenes` | 叙事 |
| PATCH | `/api/worldbuilding/:wid/scenes/:sid` | 叙事 |
| POST | `/api/worldbuilding/:wid/scenes/:sid/end` | 叙事 |
| GET | `/api/worldbuilding/:wid/time` | 时间 |
| POST | `/api/worldbuilding/:wid/time/advance` | 时间 |
| GET | `/api/worldbuilding/:wid/foreshadowing` | 伏笔 |
| POST | `/api/worldbuilding/:wid/foreshadowing` | 伏笔 |
| PATCH | `/api/worldbuilding/:wid/foreshadowing/:id` | 伏笔 |
| GET | `/api/worldbuilding/:wid/secrets` | 秘密 |
| POST | `/api/worldbuilding/:wid/secrets` | 秘密 |
| PATCH | `/api/worldbuilding/:wid/secrets/:id` | 秘密 |
| POST | `/api/worldbuilding/:wid/export` | 导出 |
| GET | `/api/worldbuilding/:wid/pipeline/state` | Pipeline |
| POST | `/api/worldbuilding/:wid/pipeline/advance` | Pipeline |
| GET | `/api/worldbuilding/pipeline/workflows` | Pipeline |
| GET | `/api/worldbuilding/pipeline/history` | Pipeline |
| POST | `/api/worldbuilding/:wid/pipeline/activate` | Pipeline |

**总计：74 个 API 端点**（28 core + 46 worldbuilding），加 1 个 SSE 流端点，加 1 个静态文件挂载（`/`）。

---

## 附录 D：已知不一致与待办

### 错误格式不一致

| 端点 | 问题 | 当前行为 |
|------|------|----------|
| `PATCH /v1/sessions/:id` | 404 返回非标准格式 | `{"error": "session not found"}` |
| `PATCH /api/worldbuilding/worlds/:id` | 404 返回非标准格式 | `{"error": "..."}` |
| `GET /api/worldbuilding/:wid/pipeline/state` | 503 返回非标准格式 | `{"error":"pipeline_not_available"}` |

### 前端已调用但后端未实现的接口

| 方法 | 路径 | 前端函数 | 说明 |
|------|------|----------|------|
| `GET` | `/v1/runs/:id/audit` | `fetchRunAudit()` | C++ 代码库中零引用，无路由注册 |

### 后端已实现但前端未对接的功能

| 功能 | 后端 | 前端状态 |
|------|------|----------|
| `POST /v1/runs/:id/ask-response` | 已注册 in `http_server.cpp:326` | `client.ts` 中无调用方法 |
| SSE `ask_user_requested` | 已推送 | `AppState.tsx` 中未处理 |
| SSE `creation_requested` / `creation_resolved` | 已推送 | `AppState.tsx` 中未处理 |
