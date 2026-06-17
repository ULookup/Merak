# Merak 架构总览

## 项目定位

Merak 是一个**叙事驱动的多 Agent 协作框架**，以 Windows 桌面端程序（Tauri 2 + WebUI）为主要交付形态。结合 AI Agent 运行时（语境管理、记忆、工具调用）与世界观构建（Worldbuilding）系统，面向长篇小说 / 角色扮演场景。

> **注意：** TUI（终端工作台）已废弃，不再维护。

---

## 一、核心架构总览

```
┌─────────────────────────────────────────────────────────────┐
│                    Desktop（Tauri 2）                        │
│  ┌───────────────────────────────────────────────────────┐ │
│  │                    WebUI（React 19）                   │ │
│  └───────────────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────────────────┤
│                      HTTP API（REST + SSE）                  │
├─────────────────────────────────────────────────────────────┤
│  Agent Loop（主循环）                                        │
│  ┌─────────┬──────────┬──────────┬──────────┬────────────┐ │
│  │ Context │  Memory  │  Tools   │  Skills  │   LLM      │ │
│  │ Pipeline│  Store   │ Registry │ Registry │ Providers  │ │
│  └─────────┴──────────┴──────────┴──────────┴────────────┘ │
├─────────────────────────────────────────────────────────────┤
│              Worldbuilding Subsystem                        │
│  ┌──────────┬──────────┬──────────┬──────────────────────┐ │
│  │ World    │ Scene    │ Pipeline │ Narrative/Agent/     │ │
│  │ Store    │ Orch.    │ Manager  │ Secret/Foreshadow    │ │
│  └──────────┴──────────┴──────────┴──────────────────────┘ │
├─────────────────────────────────────────────────────────────┤
│      Knowledge Graph (Neo4j)  │  Writer Agent              │
├─────────────────────────────────────────────────────────────┤
│  PostgreSQL + pgvector + Neo4j + 文件系统                    │
└─────────────────────────────────────────────────────────────┘
```

---

## 二、Agent Loop 主循环

```
用户输入 → AgentLoop.think()
  ├─ ContextPipeline.planned_assemble() → LLM API 消息
  ├─ LLM 返回 Text / ToolCall[]
  ├─ ToolRegistry::execute_all() → ToolResult
  └─ 反馈 LLM → 循环，直到 stop_reason="end_turn" 或达到 max_tool_turns
```

**状态机:** Thinking → Acting → Observing → Responding，通过 SSE 实时推送事件。

### 文件索引

| 文件 | 职责 |
|------|------|
| `libs/loop/src/agent_loop.cpp` | Agent 状态机主循环 |
| `libs/runtime/src/runtime_service.cpp` | Session / Run / Approval 管理 |
| `libs/runtime/src/event_bus.cpp` | SSE 事件广播 |

---

## 三、上下文组装与处理（ContextPipeline）

### 4 阶段链路

```
用户输入 + 历史 + 工具 → ContextPipeline.planned_assemble()
  │
  ├─ 1. Plan  : TokenCounter 统计 → ContextPlanner 选 CompactionTier + 分配 SectionManifest
  ├─ 2. Bind  : ContextBinder 将 11 种 SectionKind 绑定到实际数据源
  ├─ 3. Optimize: reorder → prune_schemas → microcompact → drop_rounds → spill
  └─ 4. Serialize: ContextSerializer → OpenAI/Anthropic JSON
```

### 关键设计

| 设计点 | 说明 |
|--------|------|
| **四级压缩等级** | Normal(0) → TrimSchemas(1) → CompactHistory(2) → AggressivePrune(3)，根据 token 压力自适应升级 |
| **SectionManifest 预算制** | 11 种 SectionKind 各分配独立 token 预算（Identity/Constraints/WorldContext/Skills/ToolSchemas/WorkingMemory/Memory/Conversation/Emergent*） |
| **CacheScope 层级** | Global → Session → Turn，可缓存内容排在前端以最大化 prompt caching 命中率 |
| **非破坏性压缩** | microcompact 只截断不删除；spill 将内容溢出到磁盘 |
| **独立 LLM 压缩** | Compactor 使用 gpt-4o-mini 异步生成对话摘要 |
| **自适应反馈** | PipelineStats 追踪 P50/P90 token、缓存命中率、每段实际用量 |

### 文件索引

| 文件 | 职责 |
|------|------|
| `libs/context/src/context_pipeline.cpp` | 顶层编排器 |
| `libs/context/src/context_planner.cpp` | 自适应规划器 |
| `libs/context/src/context_binder.cpp` | 数据源绑定 |
| `libs/context/src/context_optimizer.cpp` | 五种优化策略 |
| `libs/context/src/context_serializer.cpp` | OpenAI/Anthropic 双格式 |
| `libs/context/src/compactor.cpp` | LLM 驱动语义压缩 |
| `libs/context/src/cache_aware_context.cpp` | 静态/动态前缀分割 |
| `libs/context_dsl/` | 上下文模板 DSL（`@agent{...}` 等） |

---

## 四、记忆机制

### 三层架构

| 层级 | 组件 | 存储后端 | 生命周期 |
|------|------|----------|----------|
| 工作记忆 | `vector<Message>` | 内存 | 会话内 |
| 叙事工作记忆 | NarrativeWorkingMemory | 内存 | 会话内（节拍/角色状态/基调）|
| 持久记忆 | MemoryStore | PostgreSQL+pgvector(1536维) | 跨会话 |

### 核心链路

```
对话消息 → MemoryStore::append_message(msg)
  ├─ 工作记忆: 追加到 vector<Message>
  └─ 持久记忆: embed() → INSERT INTO memory_entries

记忆提取:
  对话摘要 → MemoryExtractionService (gpt-4o-mini)
  → SessionMemorySnapshot (目标/世界变化/角色变化/伏笔更新/纠正)
  → SessionJournal (JSONL日志)

记忆检索 → 注入:
  最近user消息 → MemoryStore::search(query, 5)
  → pgvector cosine距离 → MemorySnippet[]
  → ContextBinder 注入 SectionKind::Memory
```

### 文件索引

| 文件 | 职责 |
|------|------|
| `libs/memory/src/memory_store.cpp` | PostgreSQL+pgvector 记忆存储 |
| `libs/memory/src/memory_extraction_service.cpp` | LLM 驱动记忆提取 |
| `libs/memory/src/narrative_working_memory.cpp` | 叙事工作记忆 |
| `libs/memory/src/session_journal.cpp` | JSONL 会话日志 |

---

## 五、工具机制

### 分层设计

```
Tool (基类)
  ├─ Pinned Tools (8个, 始终在 prompt 中)
  │   read_file, write_file, str_replace, list_dir, grep, glob, execute_bash, tool_search
  ├─ Deferred Platform Tools (12个, 通过 tool_search 激活)
  │   git, web_fetch, web_search, lsp, symbols, memory, session, agent, task, ask_user,
  │   enter_plan_mode, exit_plan_mode
  ├─ Worldbuilding Tools (25个, 需要 Capability::Worldbuilding)
  └─ MCP Tools (运行时导入, McpToolWrapper)
```

### 工具执行链路

```
LLM 生成 ToolCall[]
  → ToolRegistry::execute_all(calls, policy)
    ├─ Sequential / Parallel / FailFast
  → Tool::execute() → std::future<ToolResult>
  → ToolResult → role="tool" Message → 反馈给 LLM
```

### 安全保障

- **三级权限**: safe / ask / deny
- **BashTool 五层安全检查**: 危险命令黑名单 → git 破坏操作 → 变量绕过 → SQL 破坏 → 沙箱路径
- **Capability 门控**: 工具可见性由 CapabilitySet 控制

---

## 六、Knowledge Graph 系统

### 数据模型

以 Neo4j 为图存储后端，提供角色、地点、组织、物品、概念五种实体类型（EntityType: Agent / Location / Organization / Item / Concept）以及 15 种关系类型。

**实体（GraphEntity）:** name + type + source_id + world_id

**关系（GraphRelation）:** 双向立场（a_to_b_stance / b_to_a_stance，13 种 Stance）、15 种关系类别（Acquaintance / Friend / Lover / Kin / MasterApprentice / SuperiorSubordinate / Enemy / Rival / Ally / Member / Owner / Guardian / Benefactor / Grudge / Custom）、事实摘要（fact / description）、关联事件（RelationEvent[]）

### Provider 接口

```
KnowledgeGraphProvider（抽象基类）
  ├─ upsert_entity / list_entities       实体增改查
  ├─ upsert_relation / delete_relation   关系增删
  ├─ query_subgraph                      子图查询
  ├─ expand                              邻居扩展
  └─ find_paths                          路径查找

Neo4jKGProvider（Neo4j 实现）
  通过 Bolt 协议连接 Neo4j，使用参数化 Cypher 查询
```

### 关系提取 Pipeline

```
场景文本 → ExtractionService (LLM 提取)
  ├─ 识别实体名 → 匹配已有 Agent
  ├─ 提取关系三元组 (source, target, kind, stance, fact)
  ├─ 去重（按 RelationKey 合并）
  └─ KG Provider upsert → Neo4j
```

### 格式化输出

`kg_formatters.cpp` 提供三种 Markdown 格式化：
- `subgraph_to_markdown` — 子图格式，按关系分组
- `neighbor_graph_to_markdown` — 邻居图格式，以中心实体展开
- `path_result_to_markdown` — 路径格式，按路径长度排列

### 文件索引

| 文件 | 职责 |
|------|------|
| `libs/knowledge_graph/src/neo4j_provider.cpp` | Neo4j Cypher 实现 |
| `libs/knowledge_graph/src/kg_formatters.cpp` | Markdown 格式化 |
| `libs/knowledge_graph/include/merak/kg/kg_models.hpp` | 数据模型定义 |
| `libs/knowledge_graph/include/merak/kg/kg_provider.hpp` | Provider 抽象接口 |
| `libs/knowledge_graph/include/merak/kg/neo4j_provider.hpp` | Neo4j Provider 声明 |
| `libs/worldbuilding/src/extraction_service.cpp` | 关系提取服务 |

---

## 七、Writer Agent

### 定位

Writer Agent 是一个**独立的 LLM 子循环**，由 God Agent 通过 `DelegateToWriterTool` 调用。它使用独立的 writer 模型、零工具、单轮生成，从结构化材料包产出场景散文。

### 调用链

```
God Agent → DelegateToWriterTool::execute()
  ├─ 加载 config/prompts/worldbuilding/writer.md 作为 system prompt
  ├─ 构造 user message：场景大纲 + 角色对话日志 + 领域数据 + 写作约束
  ├─ 创建 AgentLoop（max_turns=1, 零工具注册表）
  ├─ 调用 writer_model → 返回 scene_text
  └─ 结果 = ToolResult(scene_text)
```

### 写作约束

材料包中的约束字段：
- **style**：写作风格（如"金庸武侠风"、"现代文学"）
- **pov**：视角（第一人称 / 第三人称 / 多视角交替）
- **word_count**：目标字数范围
- **foreshadowing**：需要埋入的伏笔线索

### 关键设计

- Writer Agent 拥有**独立 LLM 实例**，与主循环模型分离，支持不同模型配置
- 零工具表确保 Writer 专注于写作，不会调用任何工具
- 单轮循环（max_turns=1），无迭代开销

### 文件索引

| 文件 | 职责 |
|------|------|
| `libs/worldbuilding/src/worldbuilding_tools.cpp` | `DelegateToWriterTool` 实现（~200 行） |
| `config/prompts/worldbuilding/writer.md` | Writer Agent system prompt |
| `libs/worldbuilding/include/merak/worldbuilding/worldbuilding_tools.hpp` | 工具声明 |
| `libs/worldbuilding/include/merak/worldbuilding/world_models.hpp` | `AgentKind::Writer` 枚举 |

---

## 八、Worldbuilding 核心链路

### 数据模型层级

```
World (世界)
  ├─ WorldKnowledge (中文全文搜索)
  ├─ Location (层次化地点)
  ├─ Agent (8种: God/Individual/Group/Manager×5)
  │   ├─ CharacterCard (角色卡，版本号乐观锁)
  │   ├─ DiaryEntry (角色日记)
  │   ├─ MemorySummary (周期性压缩)
  │   ├─ RelationEntry (双向关系)
  │   └─ VoiceFingerprint (声音指纹)
  ├─ Arc → Chapter → Section → Scene (叙事层级)
  │   ├─ TimelineEvent
  │   └─ WorldTime ({day, period})
  ├─ Foreshadowing (伏笔: Open → Paid/Abandoned)
  └─ Secret (秘密: Active → Exposed/Abandoned，信息不对等分析)
```

### 场景编排核心链路

```
1. SceneOrchestrator::prepare_scene(scene_id)
   ├─ 构建 God Context (全知视角): 章节概要+弧线目标+世界知识+伏笔+KG关系
   ├─ 为每个角色构建 CharacterContextView (受限视角): 角色卡+秘密过滤+日记
   └─ 按 AgentKind 分配工具集

2. Multi-Agent 交互: God 写作 → Character 回应 → 工具调用

3. SceneOrchestrator::finish_scene(final_markdown)
   ├─ 追加叙事 → 创建日记 → 更新声音指纹 → 记录时间线
   ├─ 自动检测伏笔（n-gram 中文模式匹配）
   └─ 检查泄密风险

4. ExtractionService (异步): 场景文本 → LLM 提取关系 → 去重 → KG upsert
```

### 知识壁垒（核心亮点）

每个角色对世界/秘密/关系的可见性不同：
- God Agent 全知视角
- Individual Agent 仅知自己应知信息
- `scene_asymmetry` 将秘密按角色过滤为 Public / Secret / Unknown
- `check_leak_risk` 自动检测角色是否泄露了不该知道的信息

### 文件索引

| 文件 | 职责 |
|------|------|
| `libs/worldbuilding/src/worldbuilding_service.cpp` | 主服务门面 |
| `libs/worldbuilding/src/scene_orchestrator.cpp` | 场景编排引擎 |
| `libs/worldbuilding/src/world_store.cpp` | 数据访问层 |
| `libs/worldbuilding/src/narrative_store.cpp` | 叙事结构管理 |
| `libs/worldbuilding/src/agent_store.cpp` | Agent 生命周期 |
| `libs/worldbuilding/src/foreshadowing_store.cpp` | 伏笔管理 |
| `libs/worldbuilding/src/secret_store.cpp` | 秘密管理 |
| `libs/worldbuilding/src/voice_analyzer.cpp` | 声音指纹分析 |
| `libs/worldbuilding/src/extraction_service.cpp` | 关系提取（连接 KG） |
| `libs/worldbuilding/src/card_access.cpp` | 角色卡访问辅助 |
| `libs/worldbuilding/schema.sql` | 数据库 Schema（17张表） |

---

## 九、PipelineManager

### 5 阶段创作工作流

```
Worldbuilding → CharacterCreation → PlotArchitecture → SceneWriting → Reflection
     世界观构建      角色创建           情节架构          场景写作        反思回顾
```

### 工作流驱动

```
PipelineManager::evaluate_and_advance(world_id)
  ├─ 加载 PipelineWorkflowDef（阶段定义）
  ├─ ConditionEvaluator::evaluate(conditions, world_context)
  │   ├─ 检查阶段入口条件（如：至少 3 个角色 → 可进入 SceneWriting）
  │   ├─ 评估自动推进规则
  │   └─ 返回 ConditionEvalSummary（all_met + 各条件详情）
  ├─ 判断是否可推进至下一阶段
  └─ 更新 PipelineState → 持久化
```

### 关键组件

| 组件 | 职责 |
|------|------|
| PipelineWorkflowDef | 定义 5 阶段、每阶段入口/出口条件、阶段内允许操作 |
| ConditionEvaluator | 评估条件表达式，支持 =/!=/</>/in/contains 操作符 |
| PipelineState | 记录当前阶段、阶段内进度、时间戳 |
| `pipeline_validation.cpp` | 工作流定义校验（循环依赖、死阶段检测） |

### 文件索引

| 文件 | 职责 |
|------|------|
| `libs/worldbuilding/src/pipeline_manager.cpp` | 工作流驱动器 |
| `libs/worldbuilding/src/condition_evaluator.cpp` | 条件评估引擎 |
| `libs/worldbuilding/src/pipeline_workflow_def.cpp` | 工作流定义 |
| `libs/worldbuilding/src/pipeline_validation.cpp` | 定义校验 |
| `libs/worldbuilding/src/pipeline.cpp` | Pipeline 数据模型 |

---

## 十、Portable Neo4j

与 Portable PostgreSQL 对等设计，为桌面端用户提供一键启动的 Neo4j 实例。

```
PortableNeo4j
  ├─ start()    → 启动 Neo4j 进程，分配端口
  ├─ stop()     → 优雅关闭
  ├─ health()   → 健康检查（Bolt 连接测试）
  └─ port()     → 返回分配端口号
```

### 文件索引

| 文件 | 职责 |
|------|------|
| `libs/portable_neo4j/src/portable_neo4j.cpp` | 进程生命周期管理 |
| `libs/portable_neo4j/include/merak/portable_neo4j.hpp` | 对外接口 |

---

## 十一、Portable PostgreSQL

随包 PostgreSQL 生命周期管理，支持 Windows 桌面端一键启动开发数据库。

```
PortablePG
  ├─ start()    → 启动 PostgreSQL 进程，分配端口
  ├─ stop()     → 优雅关闭
  ├─ health()   → 健康检查（psql 连接测试）
  └─ port()     → 返回分配端口号
```

### 文件索引

| 文件 | 职责 |
|------|------|
| `libs/portable_pg/src/portable_pg.cpp` | 进程生命周期管理 |
| `libs/portable_pg/include/merak/portable_pg.hpp` | 对外接口 |

---

## 十二、其他子系统

| 子系统 | 路径 | 职责 | 状态 |
|--------|------|------|------|
| app | `libs/app/` | Application 入口，组装各子系统 | 进行中 |
| config | `libs/config/` | 配置加载、环境变量覆盖、多优先级合并 | 已完成 |
| core | `libs/core/` | 共享类型（Message, ToolCall, ToolResult）和执行接口 | 已完成 |
| context | `libs/context/` | Token budget + 上下文压缩（4 阶段 Pipeline） | 已完成 |
| context_dsl | `libs/context_dsl/` | 上下文模板 DSL | 已完成 |
| http | `libs/http/` | REST API（cpp-httplib）+ SSE 事件流 | 已完成 |
| knowledge_graph | `libs/knowledge_graph/` | KG 数据模型 + Neo4j Provider + 格式化 | 已完成 |
| llm | `libs/llm/` | OpenAI / Anthropic Provider，统一 API 接口 | 已完成 |
| loop | `libs/loop/` | Agent 状态机（Think/Act/Observe/Respond） | 已完成 |
| mcp | `libs/mcp/` | MCP stdio client，外部工具集成 | 已完成 |
| memory | `libs/memory/` | PostgreSQL+pgvector 持久记忆 + 叙事工作记忆 | 已完成 |
| portable_neo4j | `libs/portable_neo4j/` | 随包 Neo4j 生命周期管理 | 已完成 |
| portable_pg | `libs/portable_pg/` | 随包 PostgreSQL 生命周期管理 | 已完成 |
| prompts | `libs/prompts/` | Prompt compositor，模板加载与渲染 | 已完成 |
| runtime | `libs/runtime/` | Session / Run / EventBus / Approval 管理 | 已完成 |
| skills | `libs/skills/` | 技能加载与执行 | 已完成 |
| storage | `libs/storage/` | 运行时持久化（SQLite） | 已完成 |
| tools | `libs/tools/` | 内置工具、权限控制和工具注册表 | 已完成 |
| worldbuilding | `libs/worldbuilding/` | 世界观构建、叙事管理、场景编排、Pipeline 管理 | 进行中 |

---

## 十三、子系统集成关系

```
AgentLoop (主循环)
  │
  ├── ContextPipeline ← MemoryStore (记忆检索注入)
  │                  ← WorldbuildingService (场景上下文 via DSL)
  │                  ← ToolRegistry (pinned schemas)
  │
  ├── ToolRegistry ← WorldbuildingTools (25个)
  │               ← DelegateToWriterTool → Writer Agent 子循环
  │               ← McpClient (外部MCP工具)
  │
  ├── MemoryStore ← MemoryExtractionService (异步提取)
  │
  ├── KnowledgeGraph ← ExtractionService (关系提取)
  │                 ← Neo4jKGProvider (Cypher 查询)
  │
  ├── PipelineManager ← ConditionEvaluator (阶段推进)
  │                  ← SceneOrchestrator (场景编排触发)
  │
  └── LLM Providers (Anthropic/OpenAI)
       └── ToolCall → ToolRegistry::execute_all → ToolResult → 反馈
```
