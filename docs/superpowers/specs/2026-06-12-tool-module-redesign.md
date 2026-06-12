# Tool 模块重新设计

> 状态：已确认
> 日期：2026-06-12

---

## 1. 设计目标

消除 Tool 模块的三本账问题（Tool 子类、TOOL_CATALOG、main.cpp 注册互不校验），统一 Worldbuilding 工具和平台工具的注册与发现流程，补全所有假实现，清理模块内死代码。

## 2. 工具定义：从三本账到一本账

### 2.1 当前问题

定义一个工具需要在三处分别声明，三者互不校验：

```
Tool 子类 (.cpp)  →  ToolRegistry::register_tool()  →  TOOL_CATALOG[]
    实现                   运行时注册                      元数据/发现
```

导致：
- TOOL_CATALOG 中 25 个 Worldbuilding 工具有条目但从未注册进 Registry（注册走了独立工厂）
- MultiEditTool / DeleteFileTool 已在 main.cpp 注册但 TOOL_CATALOG 中没有条目
- TOOL_CATALOG 的能力过滤与 Registry 的能力过滤各自独立

### 2.2 新设计：工具自描述

`Tool` 基类增加 `meta()` 虚方法，元数据内聚在工具实现中：

```cpp
class Tool {
public:
    virtual ~Tool() = default;
    virtual ToolSpec spec() const = 0;         // 名称、参数 schema
    virtual ToolMeta meta() const = 0;         // 触发词、是否 pin、意图类型、scope
    virtual PermissionLevel permission() const = 0;
    virtual std::future<ToolResult> execute(ToolCall call, ToolExecutionContext ctx = {}) = 0;
    virtual std::unique_ptr<Tool> clone() const = 0;
    virtual bool is_concurrent_safe(const ToolCall& call) const { return false; }
};
```

- **删除 `TOOL_CATALOG` 全局数组** — 每个 Tool 子类通过 `meta()` 自带元数据
- `ToolRegistry` 从已注册工具的 `meta()` 动态生成目录
- `search_tools`、`select_tool`、`pinned_schemas` 全部从 Registry 内部数据生成，不再查外部数组

### 2.3 删除 Capability / CapabilitySet

`tool_meta.hpp` 中的 `Capability` 枚举和 `CapabilitySet` 类删除。工具可用性不再靠静态能力过滤，改为：

- Agent 创建时决定注册哪些工具 → 每个 Agent 持有独立的 `ToolRegistry`
- 工具自身通过 `meta()` 声明所属意图类型（`IntentType`），由调用方决定是否展示

`ToolMeta` 中删除 `requires_caps` 字段。

## 3. Worldbuilding 工具统一注册

### 3.1 当前问题

Worldbuilding 工具在 `libs/worldbuilding/src/worldbuilding_tools.cpp` 中有 36 个完整的 `Tool` 子类实现，但：

- 不通过 `ToolRegistry::register_tool()` 注册
- 通过 `WorldbuildingTools::create_tools(AgentKind, ToolContext)` 工厂直接创建返回给调用方
- `TOOL_CATALOG` 只收录其中 25 个，且与实现不一致

### 3.2 新设计：工厂保留，产物注入 Registry

`WorldbuildingTools` 工厂保留（它负责按 AgentKind 筛选和注入 `ToolContext`），但生成的工具实例统一注册进 Agent 的 `ToolRegistry`：

```cpp
// 为每个 Agent 创建独立的 ToolRegistry
for (auto& agent_cfg : config.agents) {
    auto registry = std::make_shared<ToolRegistry>();

    // 平台工具
    registry->register_platform_basics();

    // Worldbuilding 工具
    auto ctx = ToolContext{wid, sid, agent_cfg.id};
    auto wb_tools = wb_factory->create_tools(agent_cfg.kind, ctx);
    registry->register_all(std::move(wb_tools));

    auto loop = std::make_unique<AgentLoop>(cfg, llm, registry, memory);
}
```

不同 AgentKind 注册的工具集不同：

| AgentKind | 平台工具 | Worldbuilding 工具 | 总计 |
|-----------|---------|-------------------|------|
| God | 全部 | 全部读写（~22个） | ~30 |
| Manager | 只读 | 对应领域（~5个） | ~8 |
| Character | 只读 | `describe_character`, `look_around`, `search_my_diary`, `write_my_diary`, `compress_my_memory` | ~5 |

### 3.3 ToolExecutionContext 扩展

Worldbuilding 工具每次 `execute()` 需要知道当前上下文。不再在工具构造时绑定 `ToolContext`，改为从 `ToolExecutionContext` 传入：

```cpp
struct ToolExecutionContext {
    std::shared_ptr<CancellationToken> cancellation;
    // 新增 — 由 AgentLoop 在每次 execute 前填充
    std::string world_id;
    std::string scene_id;
    std::string caller_agent_id;
};
```

Worldbuilding 工具实现中删除 `ToolContext ctx_` 成员，`clone()` 也不需传递 ctx。

## 4. ToolRegistry 接口

```cpp
class ToolRegistry {
public:
    // —— 注册 ——
    void register_tool(std::unique_ptr<Tool> tool);
    void register_all(std::vector<std::unique_ptr<Tool>> tools);
    void register_platform_basics();  // 一次注册 10 个平台基础工具

    // —— 执行 ——
    std::future<ToolResult> execute(const ToolCall& call, ToolExecutionContext ctx);

    // —— prompt 构建 ——
    std::vector<ToolSpec> pinned_schemas() const;   // pin 工具的完整 schema
    nlohmann::json all_tools_json() const;           // 所有工具的 LLM 格式

    // —— 工具发现（Agent 通过 ToolSearchTool 调用） ——
    std::string search_tools(const std::string& query, size_t max = 5) const;
    std::string select_tool(const std::string& name) const;

    // —— 权限 ——
    void set_permission_mode(const std::string& mode);
    bool requires_approval(const std::string& name) const;
};
```

## 5. 假实现工具：全部完整实现

仅列出 `libs/tools/` 范围内的假实现：

| 工具 | 当前问题 | 实现内容 |
|------|----------|---------|
| `WebSearchTool` | 零 HTTP 请求，返回链接让用户手动打开 | 接入搜索 API（默认 Exa，可配置），解析标题+摘要+URL |
| `ExitPlanModeTool` | plan 文本被接受后丢弃 | 保存到 SessionStore，后续 build_context 时作为 plan 上下文注入 system prompt |
| `MemoryTool feedback` action | 正面反馈只打日志不更新存储 | 补 `store->update()` 递增 weight/confidence |
| `SessionTool timeline` action | 返回空数组 `timeline_items: []` | 从 SessionStore 读取历史 turns 生成结构化时间线 |
| `SessionTool rollback` action | 返回 "not available" | 接入 `EditJournal::rollback()`，执行文件级回滚 |

## 6. 模块内死代码处理

以下均在 `libs/tools/` 范围内：

| 项 | 处理 | 原因 |
|----|------|------|
| `OutputCap` struct (`tool_registry.hpp`) | **删除** | 输出截断逻辑在 ContextOptimizer，此 struct 从未使用 |
| `ExecutionPolicy` enum + `execute_all()` | **删除** | Agent 一次只执行一个工具，批量执行场景不存在 |
| `EditFileTool` typedef (`fs_tools.hpp`) | **删除** | 从不使用的别名 |
| `BashTool::check_dangerous()` | **删除** | `safety_check()` 已被直接调用，此包装函数零调用 |
| `EditJournal::rollback()` | **接入** | 完整实现但从未调用，接入到 `SessionTool::execute("rollback")` |
| `TOOL_CATALOG` 全局数组 | **删除** | 被 `Tool::meta()` 替代 |
| `Capability` / `CapabilitySet` (`tool_meta.hpp`) | **删除** | 被 Agent 独立 Registry 替代 |

## 7. 数据流

```
启动时:
  main.cpp → registry->register_platform_basics()
           → wb_factory->create_tools(kind, ctx) → registry->register_all(...)

每轮 turn:
  AgentLoop → registry->pinned_schemas()     → 注入 system prompt
            → LLM 返回 tool_calls
            → registry->execute(call, ctx)   → ToolResult
            → registry->requires_approval()  → 用户审批（如需要）

工具发现:
  Agent → tool_search("character")
        → registry->search_tools("character")
        → [{name:"describe_character", description:"...", score:10}, ...]
  Agent → tool_search("select:describe_character")
        → registry->select_tool("describe_character")
        → {name, description, parameters:{...}}
```

## 8. 改动范围

| 文件 | 变更 |
|------|------|
| `libs/tools/include/merak/tool_base.hpp` | `Tool` 增加 `meta()` |
| `libs/core/include/merak/tool_meta.hpp` | 删 `Capability`/`CapabilitySet`，`ToolMeta` 删 `requires_caps` |
| `libs/tools/include/merak/tool_catalog.hpp` | **删除** |
| `libs/tools/include/merak/tool_registry.hpp` | 删 `OutputCap`/`ExecutionPolicy`/`execute_all`/`set_capabilities`/`set_output_caps`，加 `register_all`/`register_platform_basics` |
| `libs/tools/src/tool_registry.cpp` | `search_tools`/`select_tool`/`pinned_schemas` 改为从已注册工具动态生成 |
| `libs/core/include/merak/execution.hpp` | `ToolExecutionContext` 加 `world_id`/`scene_id`/`caller_agent_id` |
| 所有 `libs/tools/src/*.cpp` (Tool 子类) | 每个加 `meta()` 实现 |
| `libs/tools/src/web_search_tool.cpp` | 补搜索 API 实现 |
| `libs/tools/src/plan_mode_tools.cpp` | `ExitPlanModeTool` 保存 plan 到 SessionStore |
| `libs/tools/src/memory_tool.cpp` | 补 feedback 正面信号更新 |
| `libs/tools/src/session_tool.cpp` | 补 timeline + 接入 EditJournal::rollback |
| `libs/tools/src/edit_journal.cpp` | rollback 接入 SessionTool |
| `libs/tools/src/fs_tools.hpp` | 删 `EditFileTool` typedef |
| `libs/tools/src/shell_tool.cpp` | 删 `check_dangerous` |
| `libs/worldbuilding/src/worldbuilding_tools.cpp` | Worldbuilding 工具加 `meta()`，删 `ToolContext` 成员，从 `ToolExecutionContext` 读取上下文 |
| `cli/src/main.cpp` | 重构为 `register_platform_basics()` + `register_all(wb_tools)` |
