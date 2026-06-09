# Merak WebUI API Reference

> 基于 `feat/context-memory-loop-upgrade` 分支源码严格生成，2026-06-09。
> 所有接口通过 `httplib::Server` 注册在 `127.0.0.1`，返回 JSON（SSE 端点返回 `text/event-stream`）。

---

## 通用约定

### 错误响应

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

常见错误码：`session_not_found`, `session_busy`, `run_not_found`, `approval_not_found`, `invalid_request`, `invalid_path`, `file_not_found`, `file_conflict`, `version_conflict`, `world_not_found`, `agent_not_found`, `config_load_failed`, `config_save_failed`, `test_failed`, `test_unavailable`。

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
| `models` | array | 可用模型列表（为空时回退默认项） |
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

**响应 `201`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `session_id` | string | 新创建的会话 ID |
| `session` | object | 会话详情 |
| `session.id` | string | 会话 ID |
| `session.title` | string | 会话标题 |
| `session.last_seq` | number | 最后事件序号 |
| `session.created_at` | string | 创建时间 (ISO 8601) |
| `session.updated_at` | string | 更新时间 (ISO 8601) |
| `session.archived_at` | string\|null | 归档时间，未归档为 null |

---

### 1.4 `GET /v1/sessions` — 列出所有会话

**请求**：无参数

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `sessions` | array | 会话列表（按 updated_at 降序） |
| `sessions[].id` | string | 会话 ID |
| `sessions[].title` | string | 会话标题 |
| `sessions[].last_seq` | number | 最后事件序号 |
| `sessions[].created_at` | string | 创建时间 |
| `sessions[].updated_at` | string | 更新时间 |
| `sessions[].archived_at` | string\|null | 归档时间 |

---

### 1.5 `GET /v1/sessions/:id` — 获取单个会话

**路径参数**：`id` — 会话 ID

**响应 `200`**：同 session 对象（id, title, last_seq, created_at, updated_at, archived_at）

**`404`**：`session_not_found`

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

### 1.9 `GET /v1/sessions/:id/events` — 获取会话事件（轮询）

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

**事件对象通用结构**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `seq` | number | 事件序号 |
| `type` | string | 事件类型 |
| `run_id` | string | 关联的 run ID |
| `timestamp` | string | 事件时间戳 (ISO 8601) |
| `session_id` | string | 会话 ID |
| `payload` | object | 事件负载（结构因 type 而异） |

---

### 1.10 `GET /v1/sessions/:id/memory` — 获取会话消息历史

**路径参数**：`id` — 会话 ID

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `session_id` | string | 会话 ID |
| `items` | array | 消息列表 |
| `items[].index` | number | 消息序号（从 1 开始） |
| `items[].role` | string | 角色：`"user"` / `"assistant"` / `"tool"` / `"system"` |
| `items[].content` | string | 消息内容 |
| `items[].tool_call_id` | string | 关联的工具调用 ID（tool 消息时有值） |

---

### 1.11 `POST /v1/sessions/:id/runs` — 发送消息，启动 Agent 运行

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

**`409`**：`session_busy` — 会话已有未完成的 run
**`400`**：请求格式错误

---

### 1.12 `POST /v1/sessions/:id/delegations` — 创建多 Agent 委托

**路径参数**：`id` — 会话 ID

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `pattern` | string | 否 | 委托模式：`"fan_out"`(默认) / `"sequential"` / `"pipeline"` |
| `task` | string | 否 | 委托任务描述 |
| `agents` | string[] | 否 | 参与委托的 Agent ID 列表 |
| `aggregation` | string | 否 | 结果聚合策略，默认 `"all_results"` |

**响应 `202`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `delegation_id` | string | 委托 ID |
| `parent_run_id` | string | 父 run ID |
| `session_id` | string | 会话 ID |

---

### 1.13 `POST /v1/approvals/:id` — 处理审批请求

**路径参数**：`id` — 审批 ID

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `decision` | string | 是 | `"allow"` 或 `"deny"` |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `approval_id` | string | 审批 ID |
| `status` | string | 处理后的状态 |

---

### 1.14 `POST /v1/creations/:id/resolve` — 处理创建请求

用于用户对 Agent 的创建请求（如创建文件）做出决定，可附带修改。

**路径参数**：`id` — 创建请求 ID

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `decision` | string | 否 | `"allow"` 或 `"deny"`，默认 `"deny"` |
| `modifications` | object | 否 | 对创建内容的修改建议 |

**响应 `200`**：由 `RuntimeService::resolve_creation()` 返回。

**`404`**：创建请求不存在
**`400`**：请求格式错误

---

### 1.15 `POST /v1/runs/:id/cancel` — 取消运行

递归取消所有关联的子运行。

**路径参数**：`id` — run ID

**请求体**：无

**响应 `202`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `run_id` | string | run ID |
| `status` | string | `"cancelled"` |

---

### 1.16 `GET /v1/runs/:id` — 获取运行详情

**路径参数**：`id` — run ID

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `run.id` | string | run ID |
| `run.session_id` | string | 所属会话 ID |
| `run.status` | string | `"queued"` / `"running"` / `"waiting_approval"` / `"completed"` / `"failed"` / `"cancelled"` / `"interrupted"` |
| `run.model` | string | 使用的模型 |
| `run.started_at` | string | 开始时间 |
| `run.completed_at` | string\|null | 完成时间 |
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

**`404`**：`run_not_found`

---

### 1.17 `GET /v1/sessions/:id/events/stream` — SSE 事件流

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
| `text_delta` | `text` | 增量文本输出（流式打字效果） |
| `usage_updated` | `input_tokens`, `output_tokens`, `exact` | Token 用量更新 |

**工具调用**：

| event | payload 字段 | 说明 |
|-------|-------------|------|
| `tool_started` | `id`, `name`, `arguments` | 工具调用开始 |
| `tool_completed` | `id`, `name`, `output`, `is_error` | 工具调用完成 |

**审批**：

| event | payload 字段 | 说明 |
|-------|-------------|------|
| `approval_requested` | `approval_id`, `tool`, `arguments`, `tool_call_id` | 需要用户审批 |
| `approval_resolved` | `approval_id`, `decision` | 审批已处理 |

**委托**：

| event | payload 字段 | 说明 |
|-------|-------------|------|
| `delegation_started` | `delegation_id`, `pattern`, `agent_ids`, `task` | 委托启动 |
| `delegation_completed` | `delegation_id`, `status`, `aggregated_output`, `input_tokens`, `output_tokens` | 委托完成 |
| `sub_run_started` | `run_id`, `parent_run_id`, `delegation_id`, `agent_id`, `task` | 子运行开始 |
| `sub_run_completed` | `run_id`, `delegation_id`, `agent_id`, `status`, `output_preview`, `input_tokens`, `output_tokens` | 子运行结束 |
| `sub_run_tool_started` | `id`, `name`, `arguments` | 子运行工具调用开始 |
| `sub_run_tool_completed` | `id`, `name`, `output`, `is_error` | 子运行工具调用完成 |
| `sub_run_usage_updated` | `input_tokens`, `output_tokens` | 子运行 token 用量 |

**会话与故事**：

| event | payload 字段 | 说明 |
|-------|-------------|------|
| `session_created` | `title` | 会话创建 |
| `session_updated` | `title` | 会话标题已更新 |
| `story_context_updated` | `world_id`, `resource_type`, `resource_id` | 故事上下文变更 |
| `workspace_file_updated` | `path`, `version`, `updated_at` | 工作区文件被保存 |

**内部事件（不在 SSE 流中推送）**：

| event | 说明 |
|-------|------|
| `message_appended` | 消息写入历史（用 `/memory` 获取） |
| `compaction_applied` | 上下文压缩 |

---

## 2. 配置 API

### 2.1 `GET /api/config/llm` — 获取 LLM 配置

**请求**：无参数

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `provider` | string | LLM 提供商 |
| `api_key_masked` | string | 脱敏后的 API Key（如 `"********sk-abc"`） |
| `api_base_url` | string | API 基础 URL |
| `default_model` | string | 默认模型 |
| `max_output_tokens` | number | 最大输出 token 数 |

**`500`**：`config_load_failed`

---

### 2.2 `POST /api/config/llm` — 保存 LLM 配置

写入 `settings.local.json`。

**请求体**（所有字段可选，空字符串视为不修改）：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `provider` | string | 否 | LLM 提供商 |
| `api_key` | string | 否 | API Key |
| `api_base_url` | string | 否 | API 基础 URL |
| `default_model` | string | 否 | 默认模型 |
| `max_output_tokens` | number | 否 | 最大输出 token 数 |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 始终为 true |
| `restart_required` | boolean | 始终为 true，提示需要重启生效 |

---

### 2.3 `POST /api/config/llm/test` — 测试 LLM 连接

用当前配置发起轻量测试调用。

**请求**：无参数 / 无 body

**响应 `200`**：
```json
{"ok": true, "test": "passed"}
```

**`503`**：`test_unavailable`
**`502`**：`test_failed`

---

## 3. 工作区文件 API

### 3.1 `GET /api/workspace/files` — 列出工作区文件

列出工作区目录下的文本文件（md/markdown/txt/json/yaml/yml），自动忽略隐藏文件和 `runtime.db`/`sessions.db`。

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `root` | string | 否 | 工作区根目录（优先级最高） |
| `world_id` | string | 否 | 世界 ID，自动定位到 `worlds/{id}/outputs` |
| `q` | string | 否 | 搜索关键词（大小写不敏感） |
| `type` | string | 否 | 文件类型：`"all"` / `"markdown"` / `"text"` / `"data"` |

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
| `files[].dirty` | boolean | 预留字段 |

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

**`400`**：缺少 path
**`403`**：路径不在 merak home 下
**`404`**：文件不存在
**`415`**：非文本文件

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

**`400`**：缺少 path 或 content
**`403`**：路径不在 merak home 下
**`409`**：`file_conflict` — 文件已被修改（可重试）
**`415`**：非文本文件

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

---

## 4. 世界观 API

### 4.1 世界 CRUD

#### `GET /api/worldbuilding/worlds` — 列出所有世界

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

---

#### `POST /api/worldbuilding/worlds` — 创建世界

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `name` | string | 是 | 世界名称 |
| `description` | string | 否 | 世界描述 |

**响应 `201`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `world` | object | 世界详情 |
| `world_id` | string | 世界 ID |
| `name` | string | 世界名称 |
| `description` | string | 世界描述 |

---

#### `GET /api/worldbuilding/worlds/:id` — 获取世界详情（含统计）

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

---

#### `DELETE /api/worldbuilding/worlds/:id` — 删除世界

**状态**：未实现，返回 501。

---

#### `PATCH /api/worldbuilding/worlds/:id` — 更新世界

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

---

### 4.2 角色 Agent

#### `GET /api/worldbuilding/:wid/agents` — 列出 Agent

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `agents` | array | Agent 列表 |
| `agents[].id` | string | Agent ID |
| `agents[].name` | string | Agent 名称（内部标识） |
| `agents[].display_name` | string | 显示名称 |
| `agents[].kind` | string | 类型：`"individual"` / `"group"` 等 |

---

#### `POST /api/worldbuilding/:wid/agents` — 创建角色

**请求体**（CharacterCard）：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `name` | string | 是 | 角色名称（内部标识） |
| `gender` | string | 否 | 性别 |
| `age` | number | 否 | 年龄 |
| `race` | string | 否 | 种族 |
| `identity` | string | 否 | 身份/职业 |
| `emotional_tendency` | string | 否 | 情感倾向 |
| `speaking_style` | string | 否 | 说话风格 |
| `core_desire` | string | 否 | 核心欲望/动机 |
| `deep_fear` | string | 否 | 深层恐惧 |
| `daily_goal` | string | 否 | 日常目标 |
| `background` | string | 否 | 背景故事 |
| `knowledge_scope` | string | 否 | 知识范围 |
| `appearance` | string | 否 | 外貌描述 |
| `core_traits` | string[] | 否 | 核心特质 |
| `taboo_topics` | string[] | 否 | 禁忌话题 |
| `version` | number | 否 | 角色卡版本号 |
| `session_id` | string | 否 | 关联会话（用于 SSE 广播） |

**响应 `201`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `agent_id` | string | 新创建的 Agent ID |
| `name` | string | Agent 名称 |

---

#### `GET /api/worldbuilding/:wid/agents/:aid` — 获取 Agent 详情（含角色卡）

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

---

#### `PATCH /api/worldbuilding/:wid/agents/:aid` — 更新角色卡

支持乐观锁版本冲突检测。

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `fields` | object | 是 | 要更新的字段（键为 character_card 中的字段名） |
| `version` | number | 否 | 乐观锁版本号 |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `version` | number | 更新后的版本号 |

**`409`**：版本冲突，返回：
```json
{
  "ok": false,
  "error": {
    "code": "version_conflict",
    "message": "卡片已被其他来源修改，请刷新后重试",
    "current_version": <number>,
    "retryable": true
  }
}
```

---

#### `GET /api/worldbuilding/:wid/agents/:aid/diaries` — 获取角色日记

返回最近 50 条日记。

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

---

#### `POST /api/worldbuilding/:wid/agents/:aid/diaries` — 添加日记

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `content` | string | 是 | 日记内容 |
| `scene_id` | string | 否 | 关联场景 ID |
| `world_time` | string | 否 | 世界时间 |

**响应 `201`**：`{"ok": true}`

---

#### `GET /api/worldbuilding/:wid/agents/:aid/relations` — 获取角色关系

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

---

#### `GET /api/worldbuilding/agents/:aid/prompt` — 加载角色 Prompt

用于 LLM 角色扮演的 system prompt。

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `agent_id` | string | Agent ID |
| `prompt` | string | 生成的 prompt 文本 |

---

### 4.3 叙事

#### `GET /api/worldbuilding/:wid/overview` — 故事概览

故事工作台的核心数据接口，返回当前弧线、章节、场景、所有 Agent、开放伏笔、活跃秘密和世界时间。

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `session_id` | string | 否 | 关联会话 ID |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `current_arc` | object\|null | 当前故事弧线（id, title, purpose, status, updated_at） |
| `current_chapter` | object\|null | 当前章节（首个非 completed/revised 状态的章节） |
| `current_chapter.id` | string | 章节 ID |
| `current_chapter.title` | string | 章节标题 |
| `current_chapter.number` | number | 章节编号 |
| `current_chapter.status` | string | 状态：`"outline"` / `"drafting"` / `"completed"` / `"revised"` |
| `current_chapter.arc_id` | string\|null | 关联弧线 ID |
| `current_chapter.scene_count` | number | 场景数量 |
| `current_chapter.updated_at` | string | 更新时间 |
| `current_scene` | object\|null | 当前场景（首个 writing/draft 状态的场景） |
| `current_scene.id` | string | 场景 ID |
| `current_scene.title` | string | 场景标题 |
| `current_scene.chapter_id` | string | 所属章节 ID |
| `current_scene.world_time` | string | 世界时间 |
| `current_scene.status` | string | 状态：`"draft"` / `"writing"` / `"completed"` |
| `current_scene.participant_ids` | string[] | 参与角色 ID |
| `current_scene.updated_at` | string | 更新时间 |
| `agents` | array | 所有 Agent 列表（含 id, world_id, name, display_name, kind, created_at, updated_at） |
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
| `world_time` | object | 世界时间（day, period, label） |
| `world_time.day` | number | 天数 |
| `world_time.period` | number | 时段（0=晨, 1=昼, 2=午, 3=晚, 4=夜） |
| `world_time.label` | string | 时间标签（如 "第一日晨"） |

---

#### `GET /api/worldbuilding/:wid/chapters` — 列出章节

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `status` | string | 否 | 过滤：`"outline"` / `"drafting"` / `"completed"` / `"revised"` |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `chapters` | array | 章节列表（结构与 overview 中 current_chapter 相同） |

---

#### `PATCH /api/worldbuilding/:wid/chapters/:cid` — 更新章节

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `fields` | object | 是 | 要更新的字段。status 必须为 `"drafting"` / `"writing"` / `"completed"` / `"archived"` |

**响应 `200`**：`{"ok": true}`

**`409`**：版本冲突

---

#### `GET /api/worldbuilding/:wid/scenes` — 列出场景

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `chapter_id` | string | 否 | 按章节过滤 |
| `status` | string | 否 | 按状态过滤：`"draft"` / `"writing"` / `"completed"` |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `scenes` | array | 场景列表（结构与 overview 中 current_scene 相同） |

---

#### `POST /api/worldbuilding/:wid/scenes` — 创建场景

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `title` | string | 否 | 场景标题（也可用 `name`） |
| `chapter_id` | string | 是 | 所属章节 ID |
| `world_time` | string | 否 | 世界时间 |
| `narrative` | string | 否 | 叙述内容 |
| `section_id` | string | 否 | 关联区域 ID |
| `location_id` | string | 否 | 关联地点 ID |
| `participant_ids` | string[] | 否 | 参与角色 ID 列表 |
| `session_id` | string | 否 | 关联会话（用于 SSE 广播） |

**响应 `201`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `scene_id` | string | 新创建的场景 ID |

---

#### `PATCH /api/worldbuilding/:wid/scenes/:sid` — 更新场景

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `fields` | object | 是 | 要更新的字段。status 必须为 `"drafting"` / `"writing"` / `"completed"` / `"archived"` |

**响应 `200`**：`{"ok": true}`

**`409`**：版本冲突

---

#### `POST /api/worldbuilding/:wid/scenes/:sid/end` — 结束场景

触发日记撰写、关系更新和伏笔建议。

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `final_markdown` | string | 否 | 场景最终文本 |
| `session_id` | string | 否 | 关联会话（用于 SSE 广播） |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `diaries_written` | array | 已写入的日记（id, agent_id, scene_id） |
| `diary_count` | number | 日记数量 |
| `relations_updated` | number | 更新的关系数量 |
| `proposed_foreshadowing` | array | 建议的伏笔（id, content） |
| `leak_risks` | number | 秘密泄露风险数 |

---

### 4.4 时间

#### `GET /api/worldbuilding/:wid/time` — 获取世界时间

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `day` | number | 天数 |
| `period` | number | 时段（0=晨, 1=昼, 2=午, 3=晚, 4=夜） |
| `label` | string | 时间标签（中文） |

---

#### `POST /api/worldbuilding/:wid/time/advance` — 推进世界时间

**状态**：未实现，返回 501。

---

### 4.5 伏笔

#### `GET /api/worldbuilding/:wid/foreshadowing` — 列出伏笔

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `status` | string | 否 | 过滤：`"open"` / `"paid"` / `"abandoned"` |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `items` | array | 伏笔列表（结构与 overview 中 foreshadowing 相同） |
| `foreshadowing` | array | 同上（冗余字段，前端兼容） |

---

#### `POST /api/worldbuilding/:wid/foreshadowing` — 埋设伏笔

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `content` | string | 是 | 伏笔内容（也可用 `hint`） |
| `pay_off_idea` | string | 否 | 回收思路 |
| `hint_level` | string | 否 | 暗示级别：`"visible"`(默认) / `"subtle"` / `"obvious"` |
| `tags` | string[] | 否 | 标签 |
| `session_id` | string | 否 | 关联会话（用于 SSE 广播） |

**响应 `201`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `item` | object | 创建的伏笔详情 |
| `foreshadowing_id` | string | 伏笔 ID |

---

#### `PATCH /api/worldbuilding/:wid/foreshadowing/:id` — 更新伏笔

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `fields` | object | 是 | 要更新的字段。status 必须为 `"open"` / `"paid"` / `"abandoned"` |

**响应 `200`**：`{"ok": true}`

**`409`**：版本冲突

---

### 4.6 秘密

#### `GET /api/worldbuilding/:wid/secrets` — 列出秘密

**查询参数**：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `status` | string | 否 | 过滤：`"active"` / `"exposed"` / `"abandoned"` |

**响应 `200`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `items` | array | 秘密列表（结构与 overview 中 secrets 相同） |
| `secrets` | array | 同上（冗余字段，前端兼容） |

---

#### `POST /api/worldbuilding/:wid/secrets` — 创建秘密

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `holder_id` | string | 否 | 秘密持有者/标题（也可用 `title`） |
| `truth` | string | 否 | 真相 |
| `public_version` | string | 否 | 公开版本 |
| `stakes` | string | 否 | 利害关系 |
| `aware_character_ids` | string[] | 否 | 知情角色 ID |
| `suspicious_character_ids` | string[] | 否 | 怀疑角色 ID |
| `related_foreshadowing_ids` | string[] | 否 | 关联伏笔 ID |
| `session_id` | string | 否 | 关联会话（用于 SSE 广播） |

**响应 `201`**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `ok` | boolean | 操作是否成功 |
| `item` | object | 创建的秘密详情 |
| `secret_id` | string | 秘密 ID |

---

#### `PATCH /api/worldbuilding/:wid/secrets/:id` — 更新秘密

**请求体**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `fields` | object | 是 | 要更新的字段。status 必须为 `"active"` / `"revealed"` / `"abandoned"` |

**响应 `200`**：`{"ok": true}`

**`409`**：版本冲突

---

## 5. 前端已调用但后端未实现的接口

以下接口在 `webui/src/api/client.ts` 中有调用代码，但后端未注册对应路由，调用返回 404：

| 方法 | 路径 | 前端函数 | 预期用途 |
|------|------|----------|----------|
| `GET` | `/v1/runs/:id/audit` | `fetchRunAudit()` | 获取 run 审计详情 |
| `GET` | `/api/worldbuilding/:wid/chapters/:cid` | `fetchChapterContent()` | 获取章节正文内容 |

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

## 附录 B：枚举值汇总

| 枚举 | 可选值 |
|------|--------|
| RunStatus | `queued`, `running`, `waiting_approval`, `completed`, `failed`, `cancelled`, `interrupted` |
| ChapterStatus | `outline`, `drafting`, `completed`, `revised` |
| SceneStatus | `draft`, `writing`, `completed` |
| ForeshadowStatus | `open`, `paid`, `abandoned` |
| ForeshadowHintLevel | `visible`, `subtle`, `obvious` |
| SecretStatus | `active`, `exposed`, `abandoned` |
| DelegationPattern | `fan_out`, `sequential`, `pipeline` |
