# Merak 世界观 / 小说创作特化 — 设计方案

## 概述

将 Merak Agent Runtime 特化为世界观构建和大型小说创作工具。核心思想：一个 Agent 对应一个角色人物，Agent 的记忆和角色强相关，用户可创建/定义 Agent 角色。上层由上帝 Agent 统筹，中层由管理 Agent 辅助，底层角色 Agent 扮演"活生生的人"。

## World 模型与数据隔离

### World 结构

World 是顶层隔离容器。不同 World 之间的记忆、角色、场景完全隔离。

```
~/.merak/worlds/{world_id}/
├── world_knowledge/      ← 世界观公共知识
├── god/                  ← GodAgent 数据
├── agents/               ← 角色 & 管理 Agent 数据
├── scenes/               ← 场景文件
├── timeline.json         ← 时间线索引
└── sessions/             ← Session JSONL (已有)
```

### 记忆隔离级别

| 级别 | 作用域 | 存储路径 | 生命周期 |
|------|--------|---------|----------|
| World 级 | World 内所有 Agent 共享的世界观知识 | `world_knowledge/` | World 存在期间 |
| Agent 级 | 同一 Agent 的所有 Session 共享 | `agents/{id}/` | Agent 存在期间 |
| Session 级 | 单次 Session 隔离 | `sessions/{id}.jsonl` | Session 关闭即归档 |

## Agent 分层体系

### 类型树

```
Agent (抽象基类)
├── GodAgent           — 每 World 唯一，导演视角
├── ManagerAgent       — 管理类，可扩展
│   ├── MapAgent       — 地形、城市、路线
│   └── HistoryAgent   — 世界大事件时间线
└── CharacterAgent     — 角色类
    ├── IndividualAgent — 单体角色
    └── GroupAgent      — 群像容器（不发言）
```

### 各类型职责

- **GodAgent**：场景编排、时间线推进、角色调度、冲突检测、知识壁垒过滤。有全知视角，可读所有角色记忆。
- **MapAgent**：管理地形数据、城市位置、距离计算、区域查询。可派 SubAgent 处理大量地形生成。
- **HistoryAgent**：记录世界大事件、时间线查询、因果追溯。可派 SubAgent 处理多个时间线分支的更新。
- **IndividualAgent**：基于角色卡人设对话、维护个人日记记忆、情绪演化、人际关系图。通过 GodAgent 路由与其他角色互动。
- **GroupAgent**：不发言。提供共享文化/禁忌上下文、管理成员列表、维护群体记忆池。成员个体自动继承群体共享记忆。

### 约束

- 一个 World 有且仅有一个 GodAgent，World 创建时自动生成。
- ManagerAgent 可选，用户按需创建。
- Agent 之间的通信通过 GodAgent 路由，角色 A 不直接呼叫角色 B。
- SubAgent 保留作为各 Agent 的任务执行助手（批量地形生成、多时间线更新等）。

## 角色卡模板与日记记忆系统

### 角色卡 (CharacterCard)

每个角色通过结构化 Markdown 模板创建。角色卡内容作为 LLM system prompt 的核心来源。

模板字段：姓名、年龄、性别、种族、身份、核心性格特质、情绪倾向、说话风格、禁忌话题、核心欲望、深层恐惧、日常目标、背景故事、知识范围、人际关系、外貌与习惯。

角色卡**可随时间演化**（人物成长、蜕变）。每次修改保留历史版本：

```
agents/{agent_id}/
├── character_card.md              ← 当前版本
├── character_card_history/        ← 历史版本
│   ├── 2026-05-01-v1.md
│   └── 2026-06-15-v2.md
```

### 演化触发

- **作者手动**：作者直接编辑角色卡。
- **GodAgent 提议**：检测到事件累积，向作者建议角色卡变更。
- **时间推进触发**：大跨度时间跳跃时，GodAgent 基于摘要提议更新。

### 日记记忆系统

```
agents/{agent_id}/
├── diary/               ← 详细日记 (近期，按场景)
├── summaries/           ← 月度摘要 (后台生成)
├── memory_index.md      ← 记忆索引 (指向日记/摘要)
└── relations.md         ← 人际关系图 (自动维护)
```

### 记忆生命周期

- **近期 (7 天内)**：日记全文加载到 LLM 上下文。
- **中期 (7-30 天)**：后台线程生成月度摘要，上下文优先引用摘要，原文保留可查。
- **远期 (>30 天)**：仅保留摘要索引，语义检索可重新激活关联记忆。
- **极远期 (>1 年)**：可选归档日记原文，仅保留摘要。

### memory_index.md

Markdown 格式，按月组织，每条记忆指向对应的日记文件或摘要文件。人格演变事件也记录在内。

## 场景与时间线系统

### 时间线

World 级别的底层时钟。作者自由定义纪元体系（自定义年月日）。每个场景有明确时间戳。时间可跳跃，跳跃时 GodAgent 可让世界自然演化或由作者指定事件。大事件由 HistoryAgent 记录。支持回退插入闪回场景（GodAgent 检测因果冲突）。

### 场景

叙事基本单元，也是 GodAgent 的调度单位。

```
场景 {
  id, title, time, location (关联 MapAgent),
  participants (关联 CharacterAgent),
  status (draft|writing|completed),
  narrative (场景正文 Markdown)
}
```

### 场景流程

1. 作者创建场景，指定地点和参与者
2. GodAgent 场前准备：查 MapAgent 地点信息、查 HistoryAgent 相关事件、查参与者当前状态和近期记忆、组装上下文
3. 作者和角色交互（类似当前 TUI Session）
4. 场景结束，GodAgent 场后收尾：各参与者生成日记、更新人际关系、重大事件通知 HistoryAgent、如有角色卡变化信号向作者提议

## 作者交互模型

### 对话路由

| 当前对话对象 | 路由目标 |
|-------------|---------|
| 默认 (未选择) | GodAgent |
| @角色名 | IndividualAgent 直接对话 |
| @群名 | GodAgent 代表 GroupAgent 回答 |
| @map / @history | 对应 ManagerAgent |
| 场景中 | GodAgent 调度参与者 |

### TUI 命令

- World 管理：`/world list|create|use|delete`
- Agent 管理：`/agent list|create character|create manager|edit|history|delete`
- 对话切换：`@agent_name message`，`@clear`
- 场景管理：`/scene new|list|use|end|jump`
- 时间线：`/time now|advance|calendar`
- 记忆查阅：`/memory @name [latest|search]`
- 日记浏览：`/diary @name [show]`

### 权限约束

作者全知（可查阅任何角色记忆），但角色有知识壁垒（只能基于自己该知道的信息行动）。GodAgent 在准备上下文时过滤越界信息。

## GroupAgent 设计

### 结构

群像 Agent 由群体角色卡（文化、禁忌、共同历史、语言风格）、成员列表（指向 IndividualAgent）、群体记忆池、成员关系图组成。

### 成员与群体关系

个体角色拥有私有记忆（个人日记、个人角色卡）和到群体共享记忆的引用（只读）。同一事件在群体日记和个人日记中从各自视角记录。

### 响应模式

作者 `@蛮族` 时，GodAgent 查询群体角色卡和记忆池，选择合适成员代表发言，注入群体上下文 + 个人上下文。成员以个人声音 + 群体身份回复。

### 成员变更

- 新成员自动继承群体共享记忆
- 离开/死亡成员保留在列表（标记状态），共享记忆保留
- 一个角色可同时存在于多个群体

## 数据流

```
作者输入
  → TUI 路由 (对话对象)
  → GodAgent (调度)
    → 查询 MapAgent / HistoryAgent (如需)
    → 查询角色状态 & 记忆 (知识壁垒过滤)
    → 组装上下文 → LLM
    → 回复 (角色声音)
    → 场景文本写入
    → 日记生成 & 记忆索引更新
    → 关系图更新
    → SSE 返回 TUI
```

## 与现有架构的关系

- 现有 `SubAgentConfig` / `SubAgentRunner` 保留，作为各 Agent 的任务执行助手层
- 现有 `MemoryStore` 负责工作记忆 + 语义检索，日记系统是新增的持久化层
- 现有 `Session` / `Run` / `EventBus` / `RuntimeService` 保留，场景中的一次对话对应一个 Session
- 现有 `AgentLoop` / `ContextAssembler` / `Compactor` 保留，角色 Agent 使用相同机制
- `World` 是新增结构，位于 Session 之上，提供隔离和 Agent 级记忆共享

## 当前边界与排除项

- 不包含 Web UI
- 不包含鉴权和多用户
- 不包含远程边缘执行
- 后续考虑：角色自主驱动（时间推进时角色自动行动）
