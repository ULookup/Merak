# Merak 服务端 API 接口文档（WebUI 开发参考）

## 约定

- 所有请求/响应均为 `application/json`（除 SSE 为 `text/event-stream`）
- 路径中 `:id` 为占位符，替换为实际 ID
- 新增接口标有 ⭐

---

## 一、运行时元数据

### `GET /v1/runtime`

获取运行时全局信息（模型列表、工具、Agent、权限模式等）。

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
  "tools": [
    { "name": "Read", "description": "...", "source": "builtin" }
  ],
  "mcp_servers": [
    { "name": "filesystem", "alive": true }
  ],
  "agents": [
    { "id": "agent_xxx", "description": "..." }
  ],
  "delegation_patterns": ["fan_out", "sequential", "pipeline"]
}
```

> `models` 数组不会为空——若配置为空则自动回退一个默认项。

---

## 二、会话（Sessions）

### `POST /v1/sessions` — 创建会话

**请求：**
```json
{ "title": "调试认证错误" }
```
`title` 可选，不传则为空字符串。

**响应 `201`：**
```json
{
  "session_id": "session_37203685477536_1",
  "session": {
    "id": "session_37203685477536_1",
    "title": "调试认证错误",
    "last_seq": 0,
    "created_at": "2026-06-06T10:30:00Z",
    "updated_at": "2026-06-06T10:30:00Z",
    "archived_at": ""
  }
}
```

**自动命名行为**：如果创建时未传 `title`，首次发送消息后服务端会自动从消息内容截取前 50 字符作为标题。

---

### `GET /v1/sessions` — 会话列表

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

按 `updated_at` 降序排列。`title` 为空时前端应显示 "New Session"。

---

### `GET /v1/sessions/:id` — 获取单个会话

**响应 `200`：** 返回单个 `SessionSummary` 对象（同上结构）。

**错误 `404`：**
```json
{ "error": { "code": "session_not_found", "message": "Session does not exist", "retryable": false } }
```

---

### ⭐ `PATCH /v1/sessions/:id` — 重命名会话

**请求：**
```json
{ "title": "新标题" }
```

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
`updated_at` 自动更新。

**错误 `404`：** 会话不存在。

---

### ⭐ `POST /v1/sessions/:id/generate-title` — AI 生成标题

让 Agent 根据会话历史自动总结一个标题（≤50 字符）。**不写入会话历史。**

**请求：** 无 body。

**响应 `200`：**
```json
{ "title": "调试 PostgreSQL 连接池" }
```

**错误 `500`：**
```json
{ "error": { "code": "title_generation_failed", "message": "...", "retryable": false } }
```

> 前端拿到 `title` 后，应调用 `PATCH /v1/sessions/:id` 来实际更新标题，让用户有机会确认。

---

## 三、会话运行时

### `POST /v1/sessions/:id/runs` — 发送消息（启动一次运行）

**请求：**
```json
{
  "message": "帮我排查这个错误",
  "model": "claude-opus-4-7"
}
```
`model` 可选，不传则使用运行时的默认模型。

**响应 `202`：**
```json
{
  "run_id": "run_37203685477536_1",
  "session_id": "session_37203685477536_1",
  "model": "claude-opus-4-7"
}
```

**错误 `409`：** 会话已有未完成的 run（session_busy）。  
**错误 `400`：** 请求格式错误或会话不存在。

> run 的实时输出通过 SSE 流式推送（见下方 SSE 部分）。

---

### `POST /v1/sessions/:id/delegations` — 启动委托（多 Agent 协作）

**请求：**
```json
{
  "pattern": "fan_out",
  "agents": ["agent_001", "agent_002"],
  "task": "各自审查这段代码的安全性",
  "aggregation": "all_results"
}
```

| 字段 | 说明 |
|------|------|
| `pattern` | `fan_out` / `sequential` / `pipeline` |
| `agents` | 参与 Agent 的 ID 列表 |
| `task` | 委托任务描述 |
| `aggregation` | `all_results`（默认） |

**响应 `202`：**
```json
{
  "delegation_id": "deleg_xxx",
  "parent_run_id": "run_xxx",
  "session_id": "session_xxx"
}
```

---

### `GET /v1/sessions/:id/events` — 获取事件列表

**参数：** `?after=<seq>` — 只返回序号大于 `after` 的事件（可选，默认 0）。

**响应 `200`：**
```json
{
  "events": [
    {
      "seq": 1,
      "timestamp": "2026-06-06T10:30:01Z",
      "session_id": "session_xxx",
      "run_id": "run_xxx",
      "type": "run_started",
      "payload": { "message": "帮我排查这个错误" }
    }
  ]
}
```

> 内部事件类型 `message_appended` 和 `compaction_applied` 已被过滤，不在此列表中出现。用 `GET /memory` 获取消息内容。

---

### `GET /v1/sessions/:id/memory` — 获取会话消息历史

**响应 `200`：**
```json
{
  "session_id": "session_xxx",
  "items": [
    { "index": 1, "role": "user", "content": "帮我排查这个错误", "tool_call_id": "" },
    { "index": 2, "role": "assistant", "content": "我来帮你分析...", "tool_call_id": "" },
    { "index": 3, "role": "tool", "content": "...", "tool_call_id": "toolu_xxx" }
  ]
}
```

| 字段 | 说明 |
|------|------|
| `role` | `user` / `assistant` / `tool` |
| `content` | 消息文本 |
| `tool_call_id` | tool 消息与 tool_use 的关联 ID |

---

### `GET /v1/sessions/:id/events/stream` — SSE 事件流

**参数：** `?after=<seq>` — 从指定序号之后开始推送。

**响应：** `text/event-stream`（长连接，分块传输）

每帧格式：
```
id: <seq>
event: <event_type>
data: <json payload>

```

**事件类型及 payload：**

| event 类型 | payload 关键字段 | 说明 |
|-----------|-----------------|------|
| `run_started` | `message` | run 已启动 |
| `thinking` | `content` | Agent 思考过程 |
| `tool_use` | `tool_name`, `tool_args`, `tool_call_id` | 工具调用开始 |
| `tool_result` | `tool_call_id`, `content`, `is_error` | 工具调用结果 |
| `message_appended` | `role`, `content`, `tool_call_id` | ⚠️ 内部事件，SSE 中**不发送** |
| `compaction_applied` | - | ⚠️ 内部事件，SSE 中**不发送** |
| `run_completed` | `summary` | run 正常完成 |
| `run_error` | `error` | run 出错 |
| `session_updated` | `title` | ⭐ 会话标题已更新 |
| `approval_required` | `approval_id`, `tool_name`, `tool_args` | 需要用户审批 |

**keepalive：** 每秒一个 SSE 注释 `: keepalive`，防止连接超时。

---

## 四、审批

### `POST /v1/approvals/:id` — 审批决策

**请求：**
```json
{ "decision": "allow" }
```
`decision` 为 `"allow"` 或 `"deny"`。

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

**响应 `202`：**
```json
{
  "run_id": "run_xxx",
  "status": "cancelled"
}
```

---

## 六、世界（Worlds）

### `GET /api/worldbuilding/worlds` — 世界列表

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

### `POST /api/worldbuilding/worlds` — 创建世界

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

### ⭐ `PATCH /api/worldbuilding/worlds/:id` — 更新世界

**请求：**
```json
{
  "name": "赛博朋克 2077 世界观",
  "description": "重新修订的世界设定描述"
}
```
`name` 和 `description` 均为可选——只传需要更新的字段。

**响应 `200`：**
```json
{
  "ok": true,
  "world_id": "world_37203685477536_1",
  "name": "赛博朋克 2077 世界观",
  "description": "重新修订的世界设定描述"
}
```

**错误 `404`：** 世界不存在。

---

### `DELETE /api/worldbuilding/worlds/:id` — 删除世界

⚠️ **未实现**，返回 501。

---

## 七、Agent（角色）

### `GET /api/worldbuilding/:wid/agents` — 角色列表

**响应 `200`：**
```json
{
  "ok": true,
  "agents": [
    {
      "id": "agent_xxx",
      "name": "john_smith",
      "display_name": "John Smith",
      "kind": "individual"
    }
  ]
}
```

`kind` 可能值：`god` / `map_manager` / `history_manager` / `magic_system_manager` / `faction_manager` / `individual` / `group`。

---

### `POST /api/worldbuilding/:wid/agents` — 创建角色

**请求：**
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

**响应 `201`：**
```json
{
  "ok": true,
  "agent_id": "agent_xxx",
  "name": "john_smith"
}
```

---

### `GET /api/worldbuilding/:wid/agents/:aid` — 获取角色详情

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
    "created_at": "...",
    "updated_at": "..."
  }
}
```

---

### `GET /api/worldbuilding/agents/:aid/prompt` — 加载角色 System Prompt

**响应 `200`：**
```json
{
  "ok": true,
  "agent_id": "agent_xxx",
  "prompt": "You are John Smith, a private detective..."
}
```

---

## 八、叙事（Narrative）

### `POST /api/worldbuilding/:wid/scenes` — 创建场景

**请求：**
```json
{
  "title": "酒吧对峙",
  "chapter_id": "ch_xxx",
  "world_time": "第3日晚",
  "narrative": "John 走进酒吧，看到目标独自坐在角落",
  "participant_ids": ["agent_xxx", "agent_yyy"],
  "location_id": "loc_bar",
  "section_id": "sec_xxx"
}
```

**响应 `201`：**
```json
{
  "ok": true,
  "scene_id": "scene_xxx"
}
```

---

### `POST /api/worldbuilding/:wid/scenes/:sid/end` — 结束场景

**请求：**
```json
{ "final_markdown": "..." }
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

---

## 九、时间（Time）

### `GET /api/worldbuilding/:wid/time` — 当前世界时间

**响应 `200`：**
```json
{
  "ok": true,
  "day": 1,
  "period": 0,
  "label": "第一日晨"
}
```

`period`：0=晨, 1=昼, 2=午, 3=晚, 4=夜。

---

### `POST /api/worldbuilding/:wid/time/advance` — 时间推进

⚠️ **未实现**，返回 501。

---

## 十、伏笔 & 秘密

### `GET /api/worldbuilding/:wid/foreshadowing` — 伏笔列表

⚠️ **未实现**，返回 501。

### `POST /api/worldbuilding/:wid/foreshadowing` — 种植伏笔

⚠️ **未实现**，返回 501。

### `GET /api/worldbuilding/:wid/secrets` — 秘密列表

⚠️ **未实现**，返回 501。

### `POST /api/worldbuilding/:wid/secrets` — 创建秘密

⚠️ **未实现**，返回 501。

---

## 十一、配置

### `GET /api/config/llm` — 获取 LLM 配置

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

---

### `POST /api/config/llm` — 保存 LLM 配置

**请求：**
```json
{
  "provider": "anthropic",
  "api_key": "sk-ant-xxx",
  "api_base_url": "https://api.anthropic.com",
  "default_model": "claude-opus-4-7",
  "max_output_tokens": 64000
}
```
所有字段可选——只传需要更新的字段。

---

### `POST /api/config/llm/test` — 测试 LLM 连接

**响应 `200`：**
```json
{
  "ok": true,
  "message": "Connection successful"
}
```

---

## 十二、TypeScript 类型参考

```typescript
interface SessionSummary {
  id: string;
  title: string;          // ⭐ 现在会被实际填充
  last_seq: number;
  created_at: string;
  updated_at: string;
  archived_at: string | null;
}

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
  kind: string;  // "god" | "individual" | "group" | ...
}

interface ModelEntry {
  name: string;
  provider: string;
  max_context_tokens: number;
}

interface RuntimeMetadata {
  provider: string;
  model: string;
  models: ModelEntry[];
  permission_mode: string;
  memory: { enabled: boolean };
  tools: ToolSpec[];
  mcp_servers: McpServerStatus[];
  agents: AgentMetadata[];
  delegation_patterns: string[];
}
```

---

## 十三、SSE 帧结构

```
id: 42
event: thinking
data: {"seq":42,"timestamp":"...","session_id":"...","run_id":"...","type":"thinking","payload":{"content":"..."}}

```

前端解析模式：
```typescript
// 每收到一帧
const frame = { seq: parseInt(id), type: event, payload: JSON.parse(data) };
```

帧类型参考 `src/api/types.ts` 中的 `SseFrame`。

---

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-06 | ⭐ `PATCH /v1/sessions/:id` — 重命名会话 |
| 2026-06-06 | ⭐ `POST /v1/sessions/:id/generate-title` — AI 生成标题 |
| 2026-06-06 | ⭐ `PATCH /api/worldbuilding/worlds/:id` — 更新世界 |
| 2026-06-06 | ⭐ 会话自动命名：首次 run 时从消息截取标题 |
| 2026-06-06 | ⭐ SSE 新增 `session_updated` 事件 |
