# Merak API Reference（WebUI / TUI 开发参考）

## 约定

- `Content-Type`: `application/json`（SSE 为 `text/event-stream`）
- `:id` 为路径占位符
- 所有响应附带 HTTP 状态码，错误时 4xx/5xx
- 时间戳格式：ISO 8601 UTC（`2026-06-06T10:30:00Z`）

## 错误响应格式

两种格式，取决于接口：

**结构化错误（v1 接口）：**
```json
{ "error": { "code": "session_not_found", "message": "Session does not exist", "retryable": false } }
```
`retryable` 表示是否可通过重试解决。

**简单错误（worldbuilding 接口）：**
```json
{ "ok": false, "error": "world not found" }
```

---

## 一、运行时元数据

### `GET /v1/runtime`

**响应 `200`：**
```json
{
  "provider": "anthropic",
  "model": "claude-sonnet-4-6",
  "models": [
    { "name": "claude-sonnet-4-6", "provider": "anthropic", "max_context_tokens": 200000 }
  ],
  "permission_mode": "default",
  "memory": { "enabled": true },
  "worldbuilding": { "enabled": true },
  "tui": { "theme": "retro" },
  "tools": [],
  "mcp_servers": [],
  "agents": [],
  "delegation_patterns": ["fan_out", "sequential", "pipeline"]
}
```

| 字段 | 说明 |
|------|------|
| `models` | 可用模型列表，不会为空（配置空时回退默认项） |
| `tui.theme` | TUI 配色主题名 |
| `delegation_patterns` | 支持的多 Agent 协作模式 |

---

## 二、会话

### `POST /v1/sessions` — 创建

**请求：**
```json
{ "title": "" }
```
`title` 可选，不传为空。

**响应 `201`：**
```json
{
  "session_id": "session_37203685477536_1",
  "session": {
    "id": "session_37203685477536_1",
    "title": "",
    "last_seq": 0,
    "created_at": "2026-06-06T10:30:00Z",
    "updated_at": "2026-06-06T10:30:00Z",
    "archived_at": ""
  }
}
```

**自动命名：** 创建时若 `title` 为空，首次发消息后服务端自动从消息文本截取前 50 字符作为标题。若创建时传了非空 `title`，自动命名不会覆盖。

---

### `GET /v1/sessions` — 列表

**响应 `200`：**
```json
{
  "sessions": [
    {
      "id": "session_37203685477536_1",
      "title": "调试认证错误",
      "last_seq": 5,
      "created_at": "2026-06-06T10:30:00Z",
      "updated_at": "2026-06-06T10:35:00Z",
      "archived_at": ""
    }
  ]
}
```
按 `updated_at` 降序。`title` 为空时前端显示 "New Session"。

---

### `GET /v1/sessions/:id` — 详情

**响应 `200`：**
```json
{
  "id": "session_37203685477536_1",
  "title": "调试认证错误",
  "last_seq": 5,
  "created_at": "2026-06-06T10:30:00Z",
  "updated_at": "2026-06-06T10:35:00Z",
  "archived_at": ""
}
```

**`404`：** `{ "error": { "code": "session_not_found", "message": "Session does not exist", "retryable": false } }`

---

### ⭐ `PATCH /v1/sessions/:id` — 重命名

**请求：** `{ "title": "新标题" }`

**响应 `200`：**
```json
{
  "session": {
    "id": "session_37203685477536_1",
    "title": "新标题",
    "last_seq": 5,
    "created_at": "2026-06-06T10:30:00Z",
    "updated_at": "2026-06-06T10:40:00Z",
    "archived_at": ""
  }
}
```

---

### ⭐ `POST /v1/sessions/:id/generate-title` — AI 生成标题

让 Agent 根据最近 3 条用户消息总结标题（≤50 字符）。**不写入会话历史，不产生 run 记录。**

**请求：** 无 body。

**响应 `200`：**
```json
{ "title": "调试 PostgreSQL 连接池" }
```

**`500`：** `{ "error": { "code": "title_generation_failed", "message": "...", "retryable": false } }`

建议流程：拿到 `title` → 调用 `PATCH` 更新 → 用户可在此之前预览/修改。

---

## 三、消息 & 运行

### `POST /v1/sessions/:id/runs` — 发送消息

**请求：**
```json
{
  "message": "帮我排查这个错误",
  "model": ""
}
```
`model` 可选（空则用默认模型）。消息将触发 Agent 思考→工具调用→响应 的完整循环，**同步返回 run_id，异步通过 SSE 推送结果**。

**响应 `202`：**
```json
{
  "run_id": "run_37203685477536_1",
  "session_id": "session_37203685477536_1",
  "model": "claude-sonnet-4-6"
}
```

**`409`：** 会话已有未完成的 run（`session_busy`）。前端应先等待当前 run 完成。  
**`400`：** 请求格式错误。

---

### `POST /v1/sessions/:id/delegations` — 多 Agent 协作

**请求：**
```json
{
  "pattern": "fan_out",
  "agents": ["agent_001", "agent_002"],
  "task": "各自审查这段代码的安全性",
  "aggregation": "all_results"
}
```

| 字段 | 值 | 说明 |
|------|-----|------|
| `pattern` | `fan_out` `sequential` `pipeline` | 协作模式 |
| `agents` | `string[]` | 参与 Agent ID 列表 |
| `task` | `string` | 任务描述 |
| `aggregation` | `all_results` | 结果聚合方式 |

**响应 `202`：**
```json
{
  "delegation_id": "deleg_xxx",
  "parent_run_id": "run_xxx",
  "session_id": "session_xxx"
}
```

---

### `GET /v1/sessions/:id/events` — 事件列表

**查询参数：** `?after=<seq>` 只返回序号大于 `seq` 的事件（默认 0）。

**响应 `200`：**
```json
{
  "events": [
    {
      "seq": 1,
      "timestamp": "2026-06-06T10:30:01Z",
      "session_id": "session_xxx",
      "run_id": "",
      "type": "session_created",
      "payload": { "title": "" }
    }
  ]
}
```

> `message_appended` 和 `compaction_applied` 不在此列表。消息历史用 `GET /memory`。

---

### `GET /v1/sessions/:id/memory` — 消息历史

**响应 `200`：**
```json
{
  "session_id": "session_xxx",
  "items": [
    { "index": 1, "role": "user", "content": "帮我排查这个错误", "tool_call_id": "" },
    { "index": 2, "role": "assistant", "content": "我来分析...", "tool_call_id": "" },
    { "index": 3, "role": "tool", "content": "file content...", "tool_call_id": "toolu_xxx" }
  ]
}
```

| `role` | 含义 |
|--------|------|
| `user` | 用户消息 |
| `assistant` | Agent 回复（含思考后的最终文本） |
| `tool` | 工具执行结果，通过 `tool_call_id` 关联到 tool_use |

---

### `GET /v1/sessions/:id/events/stream` — SSE 流

**查询参数：** `?after=<seq>` 从指定序号之后开始推送。

**Content-Type:** `text/event-stream`（长连接，chunked）

**帧格式：**
```
id: <seq>
event: <event_type>
data: <JSON payload>

```

**keepalive：** 每秒 `: keepalive\n\n`（SSE 注释，不计入 seq）。

### SSE 事件类型

#### 运行生命周期

| event | payload 关键字段 | 说明 |
|-------|-----------------|------|
| `run_started` | `message` | 用户消息已被接受，开始处理 |
| `run_completed` | — | 正常运行结束 |
| `run_failed` | `error` | 运行异常终止 |
| `run_cancelled` | — | 运行被用户取消 |
| `run_interrupted` | `reason` | 服务重启导致运行中断 |

#### Agent 状态 & 文本流

| event | payload 关键字段 | 说明 |
|-------|-----------------|------|
| `state_changed` | `from`, `to` | Agent 状态切换（如 `thinking`→`acting`） |
| `text_delta` | `text` | 增量文本输出（流式打字效果） |
| `usage_updated` | `input_tokens`, `output_tokens`, `exact` | 令牌用量更新 |

#### 工具调用

| event | payload 关键字段 | 说明 |
|-------|-----------------|------|
| `tool_started` | `id`, `name`, `arguments` | 工具调用开始 |
| `tool_completed` | `id`, `name`, `output`, `is_error` | 工具调用完成 |

#### 审批

| event | payload 关键字段 | 说明 |
|-------|-----------------|------|
| `approval_requested` | `approval_id`, `tool`, `arguments`, `tool_call_id` | 需要用户审批 |
| `approval_resolved` | `approval_id`, `decision` | 审批已处理 |

#### 委托（Delegation）

| event | payload 关键字段 | 说明 |
|-------|-----------------|------|
| `delegation_started` | `delegation_id`, `pattern`, `agent_ids`, `task` | 委托启动 |
| `delegation_completed` | `delegation_id`, `status`, `aggregated_output`, `input_tokens`, `output_tokens` | 委托完成 |
| `sub_run_started` | `run_id`, `parent_run_id`, `delegation_id`, `agent_id`, `task` | 子运行开始 |
| `sub_run_completed` | `run_id`, `delegation_id`, `agent_id`, `status`, `output_preview`, `input_tokens`, `output_tokens` | 子运行结束 |

#### 会话

| event | payload 关键字段 | 说明 |
|-------|-----------------|------|
| `session_created` | `title` | 会话创建 |
| `session_updated` | `title` | ⭐ 会话标题已更新 |

#### 内部（SSE 不发送，仅出现在 events 列表中）

| event | 说明 |
|-------|------|
| `message_appended` | 消息写入历史（前端用 `/memory` 获取） |
| `compaction_applied` | 上下文压缩（`replaced_count`） |

---

## 四、审批

### `POST /v1/approvals/:id` — 决策

**请求：**
```json
{ "decision": "allow" }
```
值为 `"allow"` 或 `"deny"`。

**响应 `200`：**
```json
{
  "approval_id": "appr_xxx",
  "status": "allowed"
}
```

---

## 五、运行控制

### `POST /v1/runs/:id/cancel` — 取消运行

会递归取消所有关联的子运行。

**响应 `202`：**
```json
{
  "run_id": "run_xxx",
  "status": "cancelled"
}
```

---

## 六、世界

### `GET /api/worldbuilding/worlds` — 列表

**响应 `200`：**
```json
{
  "ok": true,
  "worlds": [
    {
      "id": "world_37203685477536_1",
      "name": "赛博朋克 2077",
      "description": "高科技低生活的近未来世界",
      "created_at": "2026-06-06T10:00:00Z"
    }
  ]
}
```

---

### `POST /api/worldbuilding/worlds` — 创建

**请求：**
```json
{
  "name": "赛博朋克 2077",
  "description": "高科技低生活的近未来世界"
}
```
`name` 必填，`description` 可选。

**响应 `201`：**
```json
{
  "ok": true,
  "world_id": "world_37203685477536_1",
  "name": "赛博朋克 2077"
}
```

---

### ⭐ `PATCH /api/worldbuilding/worlds/:id` — 更新

**请求：**
```json
{
  "name": "新名称",
  "description": "新描述"
}
```
两个字段均可选，只传需更新的字段。

**响应 `200`：**
```json
{
  "ok": true,
  "world_id": "world_37203685477536_1",
  "name": "新名称",
  "description": "新描述"
}
```

**`404`：** `{ "error": "world not found: world_xxx" }`

---

## 七、角色（Agents）

### `GET /api/worldbuilding/:wid/agents` — 列表

**响应 `200`：**
```json
{
  "ok": true,
  "agents": [
    { "id": "agent_xxx", "name": "john_smith", "display_name": "John Smith", "kind": "individual" }
  ]
}
```

`kind`: `god` | `map_manager` | `history_manager` | `magic_system_manager` | `faction_manager` | `individual` | `group`

---

### `POST /api/worldbuilding/:wid/agents` — 创建

**请求（CharacterCard）：**
```json
{
  "name": "john_smith",
  "gender": "男",
  "age": 35,
  "race": "人类",
  "identity": "私家侦探",
  "emotional_tendency": "冷静",
  "speaking_style": "简洁",
  "core_desire": "寻找真相",
  "deep_fear": "失去记忆",
  "daily_goal": "完成委托",
  "background": "曾是警探，因一次案件离开了警队",
  "knowledge_scope": "城市地下世界",
  "appearance": "高瘦，戴墨镜",
  "core_traits": ["敏锐", "多疑"],
  "taboo_topics": ["家人"],
  "version": 1
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `name` | `string` | **必填**，内部标识 |
| `gender` | `string` | 性别 |
| `age` | `number` | 年龄 |
| `race` | `string` | 种族 |
| `identity` | `string` | 身份/职业 |
| `emotional_tendency` | `string` | 情绪倾向 |
| `speaking_style` | `string` | 说话风格 |
| `core_desire` | `string` | 核心欲望/动机 |
| `deep_fear` | `string` | 深层恐惧 |
| `daily_goal` | `string` | 日常目标 |
| `background` | `string` | 背景故事 |
| `knowledge_scope` | `string` | 知识范围 |
| `appearance` | `string` | 外貌描述 |
| `core_traits` | `string[]` | 核心性格特征 |
| `taboo_topics` | `string[]` | 禁忌话题 |
| `version` | `number` | 卡片版本号 |

**响应 `201`：**
```json
{ "ok": true, "agent_id": "agent_xxx", "name": "john_smith" }
```

---

### `GET /api/worldbuilding/:wid/agents/:aid` — 详情

**响应 `200`：**
```json
{
  "ok": true,
  "agent": {
    "id": "agent_xxx",
    "world_id": "world_xxx",
    "name": "john_smith",
    "display_name": "John Smith",
    "kind": "individual",
    "created_at": "2026-06-06T10:00:00Z",
    "updated_at": "2026-06-06T10:00:00Z"
  }
}
```

---

### `GET /api/worldbuilding/agents/:aid/prompt` — 加载 System Prompt

**响应 `200`：**
```json
{
  "ok": true,
  "agent_id": "agent_xxx",
  "prompt": "You are John Smith, a private detective..."
}
```

---

## 八、叙事

### `POST /api/worldbuilding/:wid/scenes` — 创建场景

**请求：**
```json
{
  "title": "酒吧对峙",
  "chapter_id": "ch_xxx",
  "world_time": "第3日晚",
  "narrative": "John 走进酒吧，看到目标独自坐在角落",
  "participant_ids": ["agent_xxx"],
  "location_id": "loc_bar",
  "section_id": "sec_xxx"
}
```

`chapter_id`、`participant_ids` 必填；`section_id`、`location_id` 可选。

**响应 `201`：**
```json
{ "ok": true, "scene_id": "scene_xxx" }
```

---

### `POST /api/worldbuilding/:wid/scenes/:sid/end` — 结束场景

**请求：**
```json
{ "final_markdown": "John推开门，冷风灌入..." }
```

**响应 `200`：**
```json
{
  "ok": true,
  "diaries_written": [{ "id": "...", "agent_id": "...", "scene_id": "..." }],
  "diary_count": 3,
  "relations_updated": 2,
  "proposed_foreshadowing": [{ "id": "...", "content": "..." }],
  "leak_risks": 1
}
```

| 字段 | 说明 |
|------|------|
| `diary_count` | 写入日记的角色数 |
| `relations_updated` | 更新的角色关系数 |
| `leak_risks` | 秘密泄露风险数 |

---

## 九、时间

### `GET /api/worldbuilding/:wid/time` — 当前时间

**响应 `200`：**
```json
{
  "ok": true,
  "day": 1,
  "period": 0,
  "label": "第一日晨"
}
```

`period`: 0=晨, 1=昼, 2=午, 3=晚, 4=夜。

---

## 十、配置

### `GET /api/config/llm` — 获取

**响应 `200`：**
```json
{
  "provider": "anthropic",
  "api_base_url": "https://api.anthropic.com",
  "default_model": "claude-sonnet-4-6",
  "max_output_tokens": 32000,
  "api_key_masked": "*******sk-xxx"
}
```
`api_key_masked` 脱敏处理，不可用于前端回填。

---

### `POST /api/config/llm` — 保存

**请求（所有字段可选）：**
```json
{
  "provider": "anthropic",
  "api_key": "sk-ant-xxx",
  "api_base_url": "https://api.anthropic.com",
  "default_model": "claude-opus-4-7",
  "max_output_tokens": 64000
}
```

**响应 `200`：** `{ "ok": true }`

---

### `POST /api/config/llm/test` — 测试连接

用当前配置发起一次轻量 LLM 调用，验证连通性。

**响应 `200`：**
```json
{ "ok": true, "message": "ok" }
```

**失败：** `{ "ok": false, "message": "..." }`

---

## 十一、TypeScript 类型

```typescript
// === 会话 ===
interface SessionSummary {
  id: string;
  title: string;
  last_seq: number;
  created_at: string;
  updated_at: string;
  archived_at: string | null;
}

// === 世界 ===
interface WorldSummary {
  id: string;
  name: string;
  description: string;
  created_at: string;
}

interface WorldAgent {
  id: string;
  name: string;
  display_name: string;
  kind: string;
}

// === 运行时 ===
interface RuntimeMetadata {
  provider: string;
  model: string;
  models: ModelEntry[];
  permission_mode: string;
  memory: { enabled: boolean };
  worldbuilding: { enabled: boolean };
  tui: { theme: string };
  tools: ToolSpec[];
  mcp_servers: McpServerStatus[];
  agents: AgentMetadata[];
  delegation_patterns: string[];
}

interface ModelEntry {
  name: string;
  provider: string;
  max_context_tokens: number;
}

// === SSE ===
interface SseFrame {
  seq: number;
  type: string;
  payload: Record<string, unknown>;
  // 以下字段也在 JSON data 中：
  // timestamp: string;
  // session_id: string;
  // run_id: string;
}
```

---

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-06 | `PATCH /v1/sessions/:id` — 重命名会话 |
| 2026-06-06 | `POST /v1/sessions/:id/generate-title` — AI 生成标题 |
| 2026-06-06 | `PATCH /api/worldbuilding/worlds/:id` — 更新世界 |
| 2026-06-06 | 会话首次 run 自动从消息截取标题 |
| 2026-06-06 | SSE 新增 `session_updated` 事件 |
