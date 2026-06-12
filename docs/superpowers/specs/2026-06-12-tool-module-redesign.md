# Tool 模块重新设计

> 状态：已确认
> 日期：2026-06-12
> 关联：code-audit-fake-implementations-and-dead-code.md

---

## 1. 设计目标

消除 Tool 模块的三本账问题（Tool 子类、TOOL_CATALOG、main.cpp 注册互不校验），统一 Worldbuilding 工具和平台工具的注册与发现流程，补全所有假实现，清理死代码。

## 2. 工具定义：从三本账到一本账

### 2.1 当前问题

定义一个工具需要在三处分别声明：

```
Tool 子类 (.cpp)  →  ToolRegistry::register_tool()  →  TOOL_CATALOG[]
    实现                   运行时注册                      元数据/发现
```

三者互不校验，导致：25 个目录条目无注册、2 个工具有注册无目录条目、目录和能力系统不一致。

### 2.2 新设计：工具自描述

`Tool` 基类增加 `meta()` 虚方法：

```cpp
class Tool {
public:
    virtual ~Tool() = default;
    virtual ToolSpec spec() const = 0;         // 名称、参数 schema
    virtual ToolMeta meta() const = 0;         // 触发词、能力要求、是否 pin
    virtual PermissionLevel permission() const = 0;
    virtual std::future<ToolResult> execute(ToolCall call, ToolExecutionContext ctx = {}) = 0;
    virtual std::unique_ptr<Tool> clone() const = 0;
    virtual bool is_concurrent_safe(const ToolCall& call) const { return false; }
};
```

- 删除全局 `TOOL_CATALOG` 数组
- `ToolRegistry` 从已注册工具的 `meta()` 动态生成目录
- `select_tool`、`search_tools`、`visible_metas`、`pinned_schemas` 全部从 Registry 内部数据生成

### 2.3 注册代码变化

```cpp
// 旧：元数据散落在 TOOL_CATALOG
tools->register_tool(std::make_unique<ReadFileTool>());

// 新：元数据内聚在工具类，一行注册
tools->register_tool(std::make_unique<ReadFileTool>());
// ReadFileTool::meta() 返回 ToolMeta{.name="read_file", .pinned=true, ...}
```

## 3. 能力系统与 Worldbuilding 工具

### 3.1 当前问题

- `CapabilitySet::platform_default()` 返回空集，LspTool/MemoryTool 等默认不可见
- Worldbuilding 工具绕过 `ToolRegistry`，通过 `WorldbuildingTools::create_tools()` 工厂直接创建
- `TOOL_CATALOG` 只收录了 Worldbuilding 工具的子集（25 个 vs 实际 36 个）

### 3.2 新设计：每 Agent 独立 Registry

不同 AgentKind 的 Agent 持有不同工具集：

| AgentKind | 工具数量 | 包含 |
|-----------|---------|------|
| God | ~30 | 全部平台工具 + 全部 Worldbuilding 读写工具 |
| Manager (4种) | ~5-8 | 只读平台工具 + 对应领域的 Worldbuilding 工具 |
| Character | ~5 | `describe_character`, `search_my_diary`, `look_around`, `write_my_diary`, `compress_my_memory` |

- 删除 `Capability` / `CapabilitySet` — 工具可用性由 Agent 创建时注册决定
- `WorldbuildingTools` 工厂保留，生成的工具实例统一注册进 Agent 的 `ToolRegistry`
- `ToolExecutionContext` 扩展 `world_id` / `scene_id` / `caller_agent_id` 字段
- Worldbuilding 工具不再持有 `ToolContext` 成员，改为每次 `execute()` 从 `ToolExecutionContext` 读取

### 3.3 Agent 创建流程

```cpp
// 1. 创建共享的 WorldbuildingTools 工厂
auto wb_factory = std::make_shared<WorldbuildingTools>(service, llm);

for (auto& agent_cfg : config.agents) {
    auto registry = std::make_shared<ToolRegistry>();

    // 2. 平台基础工具
    registry->register_platform_basics();

    // 3. 领域工具（按 AgentKind 注入）
    auto ctx = ToolContext{wid, sid, agent_cfg.id};
    auto wb_tools = wb_factory->create_tools(agent_cfg.kind, ctx);
    registry->register_all(std::move(wb_tools));

    // 4. 创建 AgentLoop
    auto loop = std::make_unique<AgentLoop>(cfg, llm, registry, memory);
}
```

## 4. ToolRegistry 最终接口

```cpp
class ToolRegistry {
public:
    // —— 注册 ——
    void register_tool(std::unique_ptr<Tool> tool);
    void register_all(std::vector<std::unique_ptr<Tool>> tools);
    void register_platform_basics();  // 一次注册所有平台工具

    // —— Agent 调用 ——
    std::future<ToolResult> execute(const ToolCall& call, ToolExecutionContext ctx);

    // —— prompt 构建 ——
    std::vector<ToolSpec> pinned_schemas() const;
    nlohmann::json all_tools_json() const;

    // —— 工具发现 ——
    std::string search_tools(const std::string& query, size_t max = 5) const;
    std::string select_tool(const std::string& name) const;

    // —— 权限 ——
    void set_permission_mode(const std::string& mode);
    bool requires_approval(const std::string& name) const;
};
```

## 5. 假实现工具：全部完整实现

| 工具 | 当前问题 | 实现内容 |
|------|----------|---------|
| `WebSearchTool` | 零 HTTP 请求 | 接入搜索 API（默认 Exa），解析标题+摘要+URL |
| `ExitPlanModeTool` | plan 文本被丢弃 | 保存到 SessionStore，build_context 时注入 system prompt |
| `MemoryTool feedback` | 正面信号不更新 | 补 `store->update()` 递增 weight/confidence |
| `SessionTool timeline` | 返回空数组 | 从 SessionStore 读取历史 turns 生成结构化时间线 |
| `SessionTool rollback` | 返回 "not available" | 接入 `EditJournal::rollback()` |
| `handle_delete_world` | 返回 501 | 补 WorldStore::delete_world（含级联删除） |
| `handle_time_advance` | 返回 501 | 更新 WorldModel 时间字段，广播事件 |
| `handle_time_now` | 硬编码 day=1 | 从 WorldModel 读取实际 day/period |
| `handle_overview world_time` | 硬编码 | 同上 |
| `Emergent*` sections | 0 budget 空操作 | 删除枚举值和绑定代码 |
| `SkillExecutor fork` | 标注 "future" | 实现 context_mode=fork 调用 SubAgentRunner |
| `card_access.cpp` | 纯空壳 | 删除文件，VersionConflictError 移至 agent_store.hpp |

## 6. 死代码处理

### 直接删除（5 项）

| 项 | 原因 |
|----|------|
| `OutputCap` struct | 输出截断已在 ContextOptimizer 中实现 |
| `ExecutionPolicy` / `execute_all()` | Agent 一次只调一个工具 |
| `EditFileTool` typedef | 无人使用的别名 |
| `CacheAwareContext::append()` | 等价于 vector::push_back |
| `db_conn_` 成员 | 从未初始化 |

### 被替代（2 项）

| 项 | 替代方案 |
|----|---------|
| `Capability` / `CapabilitySet` | `Tool::meta()` + Agent 创建时决定工具集 |
| `TOOL_CATALOG` 全局数组 | `ToolRegistry` 动态生成 |

### 保留暂不动（2 项）

| 项 | 原因 |
|----|------|
| `session_store_pg.hpp/cpp` (598行) | 完整 PG 实现，为未来迁移准备 |
| `checkpoint.hpp/cpp` | 待评估与 SessionStore 内部实现的统一方案 |

### 接入已有实现（8 项）

| 项 | 接入位置 |
|----|---------|
| `EditJournal::rollback()` | `SessionTool::execute("rollback")` |
| `TurnIngestor::classify_error()` | `AgentLoop::run_loop()` 收到 HTTP error 时 |
| `LlmProvider::test_connection()` | `main.cpp` 启动时验证 API key |
| `ContextPipeline::escalate_for_recovery()` | `AgentLoop` 收到 ContextWindow 错误时 |
| `Compactor::compact_one_round()` | `ContextPipeline` context pressure 超阈值时 |
| `unregister_session_world()` / `world_session_count()` | Session 关闭时 / WorldDashboard |
| `NarrativeStore::create_story_structure()` | 创建世界后进入 Worldbuilding 阶段时 |
| `load_*_prompt()` 三个函数 | Agent 创建时按 AgentKind 加载 |

### 补 UI 接入（2 项）

| 项 | 处理 |
|----|------|
| `Sidebar.tsx` | 确认 WorldSidebar 是否已替代，若是则删除 |
| `ChapterEditor.tsx` | 接入 WorldDashboard 或 Inspector |

### 补功能实现（6 项）

| 项 | 实现内容 |
|----|---------|
| `BashTool::check_dangerous()` | 删除，safety_check 已直接调用 |
| `ReviewIssue` / `ReviewSummary` 类型 | ReflectionPhase 生成审查报告，StoryInspector 展示 |
| `MemorySummary` / `MemorySummaryListResponse` | AgentsInspector 中实现 per-agent memory 摘要 |
| `pipeline_types.hpp` 12 个 struct | 确认是否有等效定义，有则删除 |
| `RuntimeEvent` struct | 评估是否 SSE 事件流预留，否则删除 |
| `LoopHost` 抽象接口 | 确认 CLI/HTTP 是否需要，否则删除 |

## 7. 接口边界

### Agent 视角

```
Agent (每个角色一个实例)
  ├── 自己的 ToolRegistry（该角色能用的工具子集）
  ├── AgentLoop 从 LLM 响应中提取 tool_calls
  └── registry->execute(call, ctx)
```

### 工具调用流（以 Character Agent 为例）

```
1. AgentLoop 发送 turn → LLM，system prompt 含 registry->pinned_schemas()

2. LLM 返回 tool_calls: [{"name": "look_around", "arguments": "{}"}]

3. AgentLoop:
   registry->execute(ToolCall{"look_around", "{}"}, ctx)
   // ctx 携带 world_id, scene_id, caller_agent_id

4. LookAroundTool::execute():
   从 ctx 读取 scene_id → NarrativeStore::get_scene()
   → 返回场景描述文本给 LLM

5. LLM 阅读场景描述，继续写作/决策
```

### 工具发现（Agent 自行搜索）

```
Agent → tool_search("character")
     → registry->search_tools("character")
     → [{name: "describe_character", description: "...", score: 10}, ...]

Agent → tool_search("select:describe_character")
     → registry->select_tool("describe_character")
     → {name, description, parameters: {...}}
```

## 8. 数据流总览

```
启动时:
  main.cpp → registry->register_platform_basics()
           → wb_factory->create_tools(kind, ctx)
           → registry->register_all(wb_tools)

每轮 turn:
  AgentLoop → registry->pinned_schemas()     → system prompt
            → LLM 返回 tool_calls
            → registry->execute(call, ctx)   → ToolResult
            → registry->requires_approval()  → 用户审批（如需要）

错误恢复:
  AgentLoop → classify_error(http_status)     → 决定策略
            → escalate_for_recovery()         → ContextWindow 错误
            → compact_one_round()             → 高 pressure 压缩
```

## 9. 改动范围

| 模块 | 改动文件数 | 主要变更 |
|------|-----------|---------|
| `libs/tools/` | ~25 | `Tool` 加 `meta()`，删 `TOOL_CATALOG`，补假实现，删死代码 |
| `libs/worldbuilding/` | ~5 | 删 `card_access.cpp`，WorldbuildingTools 接入 Registry |
| `libs/loop/` | ~3 | 接入 `classify_error`，Worldbuilding 上下文推送 |
| `libs/context/` | ~4 | 接入 `escalate_for_recovery`、`compact_one_round` |
| `libs/llm/` | ~2 | 补缓存实现，`test_connection` 接入 main.cpp |
| `libs/http/` | ~1 | 补 delete_world、time_advance、time_now |
| `libs/runtime/` | ~2 | 评估 checkpoint 去留 |
| `libs/storage/` | 0 | `session_store_pg` 保留不动 |
| `cli/` | ~3 | 重构工具注册流程，接入 test_connection |
| `webui/` | ~5 | 接入 Sidebar/ChapterEditor，补类型使用 |
