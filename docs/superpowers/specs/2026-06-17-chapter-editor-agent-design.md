# Chapter Editor — Agent-Native 交互编辑设计

**日期：** 2026-06-17
**分支：** feat/diary-memory-p0-p1-fixes

## 概述

将 ChapterEditor 打造为 God Agent 对话的正文聚焦视图——不新增 Agent 种类，用户在章节编辑器里与 God Agent 对话，Agent 通过 4 个编辑工具操作正文，返回结构化 diff，用户逐条接受或拒绝。

参考：NovelForge 的章节编辑器体验（正文编辑 + AI 续写 + 上下文面板 + 审核），但走 Merak Agent 原生路线——对话驱动，Agent 自主推进。

## 源码基线

设计基于以下已实现的设施：

| 设施 | 位置 | 状态 |
|------|------|------|
| `Chapter.content` 字段 | `world_models.hpp:230`，PG `chapters` 表，JSON 序列化 | 已完整实现 |
| `GET /api/.../chapters/:cid` | `worldbuilding_http_handler.cpp:787` | 已实现，返回 content |
| `PATCH /api/.../chapters/:cid` | `worldbuilding_http_handler.cpp:1266` | 已实现，支持 content 更新 |
| `StyleProfile` | `world_models.hpp:32-78` | 已实现，含 `world_style_profile()` |
| `DelegateToWriterTool` | `worldbuilding_tools.cpp:3247` | 已实现，God 工具集内。临时子 AgentLoop（空 ToolRegistry, max_turns=1, temp=0.7），单轮返回 scene_text |
| `NarrativeStore::chapter_context()` | `narrative_store.hpp:71` | 已有，返回 pitch / emotional_curve / arc_purpose / open_foreshadowings / previous_scene_summaries |
| `NarrativeStore::get_chapter()` | `narrative_store.cpp:749` | 读取磁盘 JSON |
| `NarrativeStore::patch_chapter()` | `narrative_store.cpp:628` | 更新 JSON + PG |
| God Agent 工具集 | `worldbuilding_tools.cpp:3326` | 20 个工具，含 `delegate_to_writer` |
| CreativePhase 枚举 | `pipeline.hpp:10` | 6 个 Phase。**没有** Phase 9 COMPILE |
| Tool 基类 | `tool_base.hpp:15` | `spec()` / `meta()` / `execute()` / `permission()` / `clone()` |
| ToolRegistry | `tool_registry.hpp` | name-based 注册，pinned 标记控制 LLM 可见性 |
| AgentLoop::ToolExecutionContext | `agent_loop.hpp` / `.cpp:591` | 携带 `world_id, scene_id, caller_agent_id` |

## 核心设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 谁来做编辑 | God Agent（不新增 Agent） | God 已有全套工具和全局视野（20 个工具含 delegate_to_writer），编辑是其对话的一个视图模式 |
| 编辑 vs 编排 | 不切分 Session，同一对话 | 用户自然切换话题，Agent 根据上下文判断意图 |
| 操作方式 | Agent 返回结构化 diff，用户逐条接受/拒绝 | 保护用户创作主权，避免静默修改 |
| 正文定位 | 行号（1-indexed，与编辑器显示一致） | 足够精确，实现简单 |
| Diff 持久化 | 不持久化，关闭时提醒 | 减少存储复杂度，最后一道防线是前端提醒 |

## 1. 工具设计

God Agent 新增 4 个正文编辑工具，注册到 `WorldbuildingTools::create_tools(AgentKind::God)`。

所有工具遵循现有模式：
- 继承 `Tool` 基类，实现 `spec()` / `meta()` / `execute()` / `permission()` / `clone()`
- `permission()` 返回 `PermissionLevel::safe`（纯文本操作，不改变世界状态）
- `meta().intents = {IntentType::DomainWrite}`，`meta().scope = Scope::Local`
- `meta().pinned = true`（编辑模式下高频使用，直接对 LLM 可见）
- 返回格式：`ok_response({...})` / `error_response(...)`，与现有工具一致的 `{ok, data, error}` JSON

### 1.1 `WriteChapterTool`

从无到有 / 从大纲到正文。Agent 自己组织素材、决定结构。长段落（>2000 字目标）时内部委托 `DelegateToWriterTool` 的相同机制（临时子 AgentLoop + writer.md prompt）。

```
参数（JSON Schema）：
  world_id       — string，世界 ID（必填）
  chapter_id     — string，章节 ID（必填）
  position       — integer，写入行号（必填，尾部=已存在最大行号+1）
  instruction    — string，写作指令（必填）
  source_material— string，可选，参考素材
  target_words   — integer，可选，目标字数（默认取自 world.style_profile）

返回：
  {
    "ok": true,
    "data": {
      "operations": [{
        "type": "insert",
        "position": { "line": 1, "column": 0 },
        "text": "林霜推开门...",
        "reason": "写作: 根据大纲展开第一章正文"
      }]
    }
  }
```

内部流程：
1. `NarrativeStore::get_chapter(world_id, chapter_id)` 读当前正文
2. 组装上下文：`chapter_context()` + `world_style_profile()` + 场景 recap + 角色卡
3. 判断目标字数：>2000 则委托 Writer（同 `DelegateToWriterTool` 机制：临时 AgentLoop + writer.md），否则直接调 LLM 生成
4. 格式化为 diff → 返回

### 1.2 `ContinueChapterTool`

已有正文，从指定位置接着往下写。

```
参数：
  world_id       — string（必填）
  chapter_id     — string（必填）
  from_line      — integer，从第几行续写（必填，-1 = 尾部追加）
  instruction    — string（必填）
  target_words   — integer，可选（默认 500-1500）

返回：同 WriteChapterTool 格式
```

与 `WriteChapterTool` 的区别：上下文组装策略更偏接续（前文语气、节奏、未完的对话），不需要重新组织整体结构。不委托 Writer（续写段通常较短）。

### 1.3 `RewriteChapterTool`

替换正文中指定范围的内容。

```
参数：
  world_id       — string（必填）
  chapter_id     — string（必填）
  start_line     — integer（必填，1-indexed）
  end_line       — integer（必填，1-indexed，含 end_line 行）
  instruction    — string（必填）

返回：
  {
    "ok": true,
    "data": {
      "operations": [{
        "type": "replace",
        "range": { "start": {"line": 15, "column": 10}, "end": {"line": 17, "column": 5} },
        "old_text": "她笑了笑。",
        "new_text": "她嘴角动了一下，没有笑出来。",
        "reason": "改写: 林霜此时应该更不安"
      }]
    }
  }
```

内部：读选中段 + 前后各 10 行上下文 → 查出场角色/出场地点/风格 → 生成替换文本 → 格式化为 diff。

### 1.4 `ReviewChapterTool`

扫描正文并标注问题。只读，不产生正文变更。

```
参数：
  world_id       — string（必填）
  chapter_id     — string（必填）
  start_line     — integer，可选（默认 1）
  end_line       — integer，可选（默认全文最后一行）
  aspects        — string，可选，默认 "all"（"consistency"|"style"|"foreshadowing"|"all"）

返回：
  {
    "ok": true,
    "data": {
      "operations": [{
        "type": "annotation",
        "position": { "line": 23 },
        "message": "林霜的发色与角色卡不符（角色卡: 黑色，此处: 灰白）",
        "severity": "warn"
      }]
    }
  }
```

审核维度：
- `consistency` — 角色人设、场景设定、时间线一致性（对比角色卡、KG 关系、`chapter_context()` 中的场景 recap）
- `style` — `world_style_profile()` 风格配置符合度、语法、节奏、描写密度
- `foreshadowing` — 该埋伏笔的地方、`chapter_context()` 中 open_foreshadowings 的推进状态

`meta().intents = {IntentType::DomainRead}`，`meta().pinned = true`。

### 工具对比

| 工具 | 用途 | 有正文前提 | 调 Writer | Intent |
|------|------|-----------|-----------|--------|
| `write_chapter` | 从无到有 / 大纲展开 | 否 | 长段落时 | DomainWrite |
| `continue_chapter` | 从已有位置续写 | 是 | 否 | DomainWrite |
| `rewrite_chapter` | 替换指定段落 | 是 | 否 | DomainWrite |
| `review_chapter` | 扫描标注问题 | 是 | 否 | DomainRead |

### 与现有工具的协作

```
God Agent 在编排场景时:
  end_scene → delegate_to_writer → 拿到 scene_text
  → write_chapter(chapter_id, position=尾部, instruction="写入场景正文")
  → 正文落库

God Agent 在编辑章节时:
  review_chapter → 发现问题
  → rewrite_chapter / continue_chapter → 修改
```

`delegate_to_writer` 已存在，是 God Agent 工具集第 21 个工具。场景编排水线不变——God 调 `delegate_to_writer` 拿初稿，再调 `write_chapter` 写入 chapter.content。不新增 God Agent Phase。

## 2. Session 上下文

打开章节编辑器时，God Agent 对话的 system message 中注入编辑上下文。利用现有的上下文组装基础设施：

### 注入数据

| 数据 | 来源 | 说明 |
|------|------|------|
| 本章正文 | `NarrativeStore::get_chapter()` → `content` | 全文，带隐式行号（1-indexed） |
| 本章上下文 | `NarrativeStore::chapter_context()` | pitch、emotional_curve、arc_purpose、open_foreshadowings、前场景摘要 |
| 出场角色 | 从场景参与者提取（`WorldbuildingService::agents()`） | 角色卡摘要（人设、当前状态） |
| 出场地点 | 从场景数据提取 | 地点描述 |
| 风格配置 | `world_style_profile(world)` | 已实现的 `StyleProfile` 结构 |
| 前章衔接 | `get_chapter(wid, cid-1)` → content 最后 200 字 | 保证衔接感 |

### 格式

```markdown
[编辑模式] 当前聚焦: 《{world.title}》第{chapter.number}章 "{chapter.title}"

## 正文
{chapter.content}

## 本章上下文
{chapter_context 的输出整合}

## 关键角色
- {name} ({identity}): {personality_summary}。当前状态: {current_state}

## 地点设定
- {name}: {description}

## 风格约束
{world_style_profile 完整内容}

## 前章衔接
- 第{number-1}章结尾: {last_200_chars_of_previous_content}
```

### 注入方式

不走 `PromptCompositor` 的正式 section（那是编排场景用的），而是作为 system message 的附加段落追加——类似 `SceneOrchestrator::prepare_scene()` 构建 `god_context` 的方式（`scene_orchestrator.cpp:202-281`）。

### 更新策略

- 打开编辑器时：一次性注入完整上下文
- 正文被接受后（PATCH 落库后）：更新上下文中的正文为新版本
- 切到其他章节：清空旧上下文，注入新章节
- 第一章无前章衔接时：对应字段填入 `"（无——本章为第一章）"`
- 用户提到其他设定/角色：Agent 用自己的现有工具按需查询（`read_character_card`, `query_world` 等）
- diff 可部分接受：用户逐条独立操作，已接受的变更立即生效到工作副本，未处理的保持 pending

## 3. God Agent 提示词调整

在 `config/prompts/worldbuilding/god.md` 中新增编辑模式行为定义。god.md 已按 10-section 模板组织（`<agent_role>` 到 `<final_reminder>`），在 `<operating_rules>` 末尾追加：

```markdown
### 编辑模式

当 system message 中出现 `[编辑模式]` 标记和章节正文时，你进入编辑模式。
此时你的行为优先级：

P0（绝对遵守）：
1. 正文是工作焦点。回应首先考虑对正文的影响，而非世界状态变更。
2. 使用 write_chapter / continue_chapter / rewrite_chapter / review_chapter 工具
   操作正文，它们返回结构化 diff。不要直接在对话中输出正文。
3. 不要静默修改正文。每次修改都通过工具返回 diff，让用户审阅。

P1（高优先级）：
4. 不要擅自推进剧情。编辑模式下只打磨文字，不调用 advance_world_time、
   create_scene、end_scene 等世界状态变更工具。
5. 当用户意图涉及剧情决策（"林霜应该在这里离开"、"加一个新角色"），
   提醒用户这是编排层面的变更，建议在场景编排中处理。

P2（默认）：
6. 主动标注问题。如果发现正文与角色设定/场景描述/前文情节矛盾，
   主动使用 review_chapter 标注，不要默默修正。
7. 保持风格一致。始终参照风格约束（StyleProfile）检查正文。
```

## 4. 工具注册

### WorldbuildingTools 变更

`worldbuilding_tools.cpp:3355` 附近，在 God Agent 的工具创建 switch case 中新增：

```cpp
case AgentKind::God:
    // ... 现有 20 个工具 ...
    tools.push_back(std::make_unique<DelegateToWriterTool>(...));
    tools.push_back(std::make_unique<WriteChapterTool>(service_, narrative_));
    tools.push_back(std::make_unique<ContinueChapterTool>(service_, narrative_));
    tools.push_back(std::make_unique<RewriteChapterTool>(service_, narrative_));
    tools.push_back(std::make_unique<ReviewChapterTool>(service_, narrative_));
```

4 个新工具依赖 `WorldbuildingService&` 和 `NarrativeStore&`（读取章节、查询角色/设定、访问 StyleProfile）。

### AgentLoop 兼容性

`AgentLoop::handle_tool_calls()` 已通过 `ToolExecutionContext` 传递 `world_id, scene_id, caller_agent_id`。编辑工具从 `exec_ctx.world_id` 获取世界上下文，不需要新增参数传递。

## 5. API 层

### 已有（无需改动）

| 端点 | 用途 | 实现位置 |
|------|------|---------|
| `GET /api/worldbuilding/:wid/chapters/:cid` | 读取章节（含 content） | `worldbuilding_http_handler.cpp:787` |
| `PATCH /api/worldbuilding/:wid/chapters/:cid` | 更新章节（title, content 等） | `worldbuilding_http_handler.cpp:1266` |

### 新增

`GET /api/worldbuilding/:wid/chapters/:cid/context`

一次性返回编辑器初始化所需全部上下文，聚合已有服务的现有方法：

```json
{
  "ok": true,
  "chapter": { "id": "...", "title": "...", "number": 1, "content": "...", "pitch": "..." },
  "chapter_context": {
    "arc_purpose": "...",
    "emotional_curve": [...],
    "open_foreshadowings": [{ "id": "...", "title": "...", "progress": "..." }],
    "previous_scene_summaries": ["场景1: ...", "场景2: ..."]
  },
  "characters": [{ "id": "...", "name": "林霜", "identity": "主角", "summary": "..." }],
  "locations": [{ "id": "...", "name": "狼烟旅店", "description": "..." }],
  "style_profile": { "name": "...", "target_word_count_min": 800, "target_word_count_max": 2000, "taboos": [...] },
  "previous_chapter_recap": "..."
}
```

实现：`WorldbuildingHttpHandler::handle_get_chapter_context()` 聚合调用：
- `NarrativeStore::get_chapter(wid, cid)` → chapter 数据
- `NarrativeStore::chapter_context(wid, cid)` → 上下文（已有方法）
- `WorldbuildingService::agents().list_by_world(wid)` 过滤场景参与者 → characters
- 从 scene data 提取 → locations
- `world_style_profile(world)` → style_profile（已有函数）
- `NarrativeStore::get_chapter(wid, cid-1)` 取 content 尾段 → previous_chapter_recap

## 6. Diff 生命周期

Diff 不单独持久化（不新增 storage 层）：

1. Agent 返回 diff JSON → 前端展示，工作副本包含 pending 变更
2. 用户接受某条 operation → 变更应用到工作副本，标记 accepted
3. 用户拒绝某条 operation → 工作副本回退对应段，标记 rejected
4. 用户点保存 → `PATCH` 将工作副本（含所有 accepted 变更）写入后端
5. 关闭编辑器前有未处理 pending → 前端弹提醒

## 7. Diff 格式

```json
{
  "operations": [
    {
      "type": "insert",
      "position": { "line": 42, "column": 0 },
      "text": "林霜推开门，冷风灌了进来。",
      "reason": "续写: 承接上一段，推进场景"
    },
    {
      "type": "replace",
      "range": {
        "start": { "line": 15, "column": 10 },
        "end": { "line": 17, "column": 5 }
      },
      "old_text": "她笑了笑。",
      "new_text": "她嘴角动了一下，没有笑出来。",
      "reason": "改写: 林霜此时应该更不安"
    },
    {
      "type": "annotation",
      "position": { "line": 23 },
      "message": "此处林霜的发色与角色卡不符（角色卡: 黑色，此处: 灰白）",
      "severity": "warn"
    }
  ]
}
```

每个 operation 独立状态：`pending` → `accepted` / `rejected`。`annotation` 类型无 accept/reject，只有 dismiss。

## 8. 变更文件清单

| 文件 | 变更 | 大小 |
|------|------|------|
| `config/prompts/worldbuilding/god.md` | `<operating_rules>` 末尾追加编辑模式规则 | ~20 行 |
| `libs/worldbuilding/src/worldbuilding_tools.cpp` | 新增 4 个 Tool 类实现 + God case 注册 | ~300 行 |
| `libs/worldbuilding/include/merak/worldbuilding/worldbuilding_tools.hpp` | 4 个 Tool 类声明 | ~80 行 |
| `libs/http/src/worldbuilding_http_handler.cpp` | 新增 `handle_get_chapter_context()` + 路由注册 | ~60 行 |
| `libs/http/include/merak/worldbuilding_http_handler.hpp` | 新增 handler 声明 | ~1 行 |

### 不需要修改

- `world_models.hpp` — `Chapter.content` 和 `StyleProfile` 已完整实现
- `narrative_store.hpp/.cpp` — `get_chapter` / `patch_chapter` / `chapter_context` 已就绪，编辑工具直接调用这些方法
- `pipeline.hpp` — 不新增 CreativePhase
- `scene_orchestrator.cpp` — 编排流程不变
- `god.md` 其他 section — 保持现有结构
- `writer.md` — Writer 行为不变（仍是无工具单轮产出）

## 9. 设计记录

| 决策 | 选择 | 理由 |
|------|------|------|
| 不新增 Editor Agent | God Agent 承担编辑职责 | God 已有 20 个工具含 delegate_to_writer，编辑是其对话的视图模式 |
| 不切分编排/编辑 Session | 同一 Session 内自由切换 | 用户自然切换话题，上下文注入驱动行为聚焦 |
| Diff 不持久化 | 前端临时持有，保存时 PATCH 落库 | 复用已有 `PATCH chapter` 端点；关闭提醒兜底 |
| 行号定位 | 1-indexed 行列号 | 实现简单，精度足够，与编辑器显示一致 |
| 上下文端点聚合 | 单次 `/context` 请求 | 复用已有 `chapter_context()` 等方法，减少前端 N+1 |
| Writer 仍可被调 | `write_chapter` 长段落时内部委托（同 `DelegateToWriterTool` 机制） | 复用已有 writer.md + 子 AgentLoop 基础设施 |
| 工具注册在 WorldbuildingTools | God case 追加 4 个工具 | 与现有 20 个 God 工具注册方式一致 |
| 不新增 CreativePhase | 编辑是 Phase 外交互 | `CreativePhase` 枚举只管编排流水线，编辑不改变世界状态 |
