# 设计：Agent 工业级加固（四批修复方案）

**日期:** 2026-06-21
**范围:** 3 CRITICAL + 14 HIGH + 12 MEDIUM 问题，覆盖护栏、子 Agent、可观测性、工具系统
**参考:** Claude Code 权限模型、Codex CLI 可观测性模式
**分支:** `infra-fixes-2026-06-20`

---

## 概述

通过 4 批次修复 Agent 框架 29 个问题（3 CRITICAL, 14 HIGH, 12 MEDIUM）。每批独立可测，遵循项目现有 Config 结构体 + 构造注入模式。预计总 diff ~780 行，涉及 22 个文件。

---

## 现有工具系统架构分析

设计前对工具模块的完整审查，确保改动与现有架构兼容。

### 现有三层分类

工具系统当前有两层独立分类，加上本次新增为三层：

| 层 | 位置 | 枚举 | 用途 | 消费方 |
|---|------|------|------|--------|
| 权限风险 | `ToolSpec::category` | `Category { ReadOnly, Consultative, Mutating, Shell }` | Plan mode 工具拒绝 | `agent_loop.cpp:478` |
| 功能领域 | `ToolMeta::intents` | `IntentType { CodeEdit, CodeRead, DomainRead, DomainWrite, Git, Network, CodeIntel, Memory, Introspect, AgentOp, TaskMgmt }` | 工具搜索/分类 | `search_tools()` |
| **护栏行为（新增）** | **`ToolMeta::domain`** | **`ToolDomain { General=0, Write=1<<0, WorldQuery=1<<1 }`** | **TurnGuard 失速检测** | **`agent_loop.cpp:300`** |

三层互不冲突：
- `Category` 是权限维度（ReadOnly/Consultative/Mutating/Shell），用于 plan mode 拒绝 Mutating 工具
- `IntentType` 是功能领域维度（CodeEdit/DomainRead/Network/...），用于工具搜索，支持多值
- `ToolDomain` 是护栏行为维度（Write/WorldQuery/General），用于 TurnGuard 检测 "只读不写" 和 "只查世界不出内容" 两种失速模式

### 为什么不复用 IntentType？

`IntentType` 看起来有 `DomainRead`/`DomainWrite`/`CodeEdit`/`CodeRead`，理论上可以用于 TurnGuard 判断。但存在三个问题：

1. **IntentType 是多值 vector**。`BashTool` 同时有 `{CodeEdit, CodeRead, Git}` 三个 intent。TurnGuard 的 `had_world_query_only` 判断需要 "ALL tools exclusively world-query"，多值判断逻辑别扭且易错
2. **非 WorldBuilding 工具缺乏合适的 intent**。`bash`、`web_fetch`、`web_search` 这类通用工具没有 `DomainRead`/`DomainWrite` 意图，但它们需要被 TurnGuard 正确分类（归类为 General，不属于 WorldQuery）
3. **IntentType 侧重功能归属**（"这个工具属于哪个领域"），而 TurnGuard 需要的是**行为分类**（"这个工具是否产出内容"）。两者语义不同：`BashTool` 有 `IntentType::Git`，但 TurnGuard 不关心它是否操作 Git——只关心它是否写内容

位掩码 `ToolDomain` 更简洁，语义清晰，且支持未来扩展（如 `ReadOnly = 1<<2` 表示纯读取工具）。

### 现有 WorldBuilding 工具的 Category 问题

审查发现 WorldBuilding 写工具（`create_character`, `create_scene`, `advance_world_time` 等）全部依赖 `ToolSpec::category` 默认值 `Category::ReadOnly`，未显式设为 `Category::Mutating`。这意味着 **plan mode 无法正确拒绝 WorldBuilding 写操作**。这也解释了为什么 `agent_loop.cpp:300-305` 需要硬编码工具名列表——`Category` 枚举不足以做 TurnGuard 判断。

本次 B1 的 `ToolDomain` 方案直接解决这个分类缺口。实现时 WorldBuilding 写工具应同步补设 `s.category = Category::Mutating`（独立改进，不影响 B1 方案）。

### `register_tool()` 兼容性

`register_tool()` 当前只读 `tool->spec()`，取 name 和 source。B1 设计需要额外读 `tool->meta()` 取 `domain` 存入 `domains_` map：

```cpp
void ToolRegistry::register_tool(std::unique_ptr<Tool> tool) {
    auto spec = tool->spec();
    auto meta = tool->meta();          // 新增
    std::string name = spec.name;
    source_[name] = spec.source;
    domains_[name] = meta.domain;      // 新增
    tools_[name] = std::move(tool);
}
```

改动仅 2 行，不影响现有 `tools_`/`source_` 逻辑。`domains_` 是与 `source_` 并列的独立 map，无竞争。

### `validate_arguments()` 与现有模式对齐

`tool_registry.cpp:155` 已有匿名 namespace 中的 `match_score()` 辅助函数。B4 的 `validate_arguments()` 放在同匿名 namespace 中，遵循相同 convention。校验使用 `nlohmann::json`（工程已有依赖），不引入新库。

### 结论

B1 和 B4 的工具相关改动与现有架构完全兼容。`ToolDomain` 是独立于 `Category` 和 `IntentType` 的第三维分类，不产生语义重叠或冲突。

---

## Batch 1：护栏可配置化

**修复：** 4 HIGH
**预估 diff：** ~150 行

### 1a. TurnGuardConfig 结构体

**问题：** `turn_guard.cpp` 中 6 个阈值硬编码为 magic number，5 条 Nudge 消息硬编码中文。

**方案：** 在 `turn_guard.hpp` 新增 `TurnGuardConfig` 结构体，对齐 `StallDetector::Config` 模式。默认值英文。

```cpp
// libs/loop/include/merak/turn_guard.hpp 新增结构体
struct TurnGuardConfig {
    int max_consecutive_world_query_rounds = 5;
    int max_consecutive_read_only_rounds = 3;
    int max_consecutive_content_avoidance = 3;
    int max_tool_calls_per_round = 15;
    int max_warnings_before_critical = 4;

    std::string nudge_write_now =
        "You've gathered a lot of information. It's time to start writing content.";
    std::string nudge_accept_imperfection =
        "Accept imperfection — write it down first, you can revise later.";
    std::string nudge_check_duplicates =
        "Check whether a character or location with the same name already exists.";
    std::string nudge_tone_consistency =
        "Mind your narrative tone — keep it consistent with the scene's era and setting.";
    std::string nudge_try_write_tool =
        "Try using your write tool to get your thoughts onto the page.";
    std::string nudge_prefix = "[Nudge] ";
};
```

`TurnGuard` 构造函数接受 `TurnGuardConfig`，默认值维持现有行为。`evaluate()` 使用 `config_.max_consecutive_world_query_rounds` 等字段替代 magic number。

**文件：** `libs/loop/include/merak/turn_guard.hpp`, `libs/loop/src/turn_guard.cpp`

### 1b. ToolCategory 标志位 — 消除工具名硬编码

**问题：** `agent_loop.cpp:300-316` 硬编码两处工具名字符串列表（write tools / world-query tools），工具增删改名导致静默失效。

**方案：** 在 `ToolMeta` 结构体增加 `ToolDomain` 标志位，`ToolRegistry` 提供 `domain_of()` 查询方法。

注意：`tool_meta.hpp:30` 已有 `enum class Category { ReadOnly, Consultative, Mutating, Shell }` 用于权限风险分级（plan mode 工具拒绝时使用）。新增的 `ToolDomain` 是正交概念——功能领域分类（写内容 vs 查世界 vs 通用），两者不能混合。

```cpp
// libs/core/include/merak/tool_meta.hpp 新增枚举（独立于已有 Category）
enum class ToolDomain : uint8_t {
    General    = 0,
    Write      = 1 << 0,
    WorldQuery = 1 << 1,
};
```

`ToolMeta` 新增字段（默认 General = 0，未显式设置的工具不会读取到未初始化值）：

```cpp
// tool_meta.hpp ToolMeta 结构体新增
ToolDomain domain = ToolDomain::General;
```

各工具注册时声明 domain：

| 工具 | Domain |
|------|--------|
| `write_file`, `str_replace` | `ToolDomain::Write` |
| `create_character`, `create_scene`, `create_chapter` | `ToolDomain::Write` |
| `create_location`, `add_world_knowledge` | `ToolDomain::Write` |
| `plant_foreshadowing`, `expose_secret` | `ToolDomain::Write` |
| `query_map`, `query_world`, `query_history` | `ToolDomain::WorldQuery` |
| `query_magic`, `query_faction` | `ToolDomain::WorldQuery` |
| `search_agent`, `look_around` | `ToolDomain::WorldQuery` |
| `read_character_card`, `read_secret` | `ToolDomain::WorldQuery` |
| `read_foreshadowing`, `search_my_diary` | `ToolDomain::WorldQuery` |
| `read_file`, `bash`, `web_fetch`, `web_search`, `git_*`, `lsp_*`, `symbols_*` | `ToolDomain::General` |
| `agent`, `task`, `ask_user`, `memory_*`, `session_*` | `ToolDomain::General` |
| `tool_search`, `enter_plan_mode`, `exit_plan_mode` | `ToolDomain::General` |

`agent_loop.cpp` 的判断逻辑改为从 `ToolRegistry` 查询：

```cpp
bool had_write = false;
bool had_world_query_only = true;
for (auto& tc : accumulated_tool_calls) {
    auto dom = tools_->domain_of(tc.name);
    if (dom & ToolDomain::Write) {
        had_write = true;
        had_world_query_only = false;
    }
    if (!(dom & ToolDomain::WorldQuery)) {
        had_world_query_only = false;
    }
}
```

`ToolRegistry` 新增 `domain_of()` 和内部 map：

```cpp
// libs/tools/include/merak/tool_registry.hpp
ToolDomain domain_of(const std::string& name) const {
    auto it = domains_.find(name);
    return it != domains_.end() ? it->second : ToolDomain::General;
}

private:
    std::map<std::string, ToolDomain> domains_;
```

`register_tool()` 中从 `tool->meta().category` 读取并填入 `categories_`。

**文件：** `libs/tools/include/merak/tool_registry.hpp`, `libs/tools/src/tool_registry.cpp`, `libs/core/include/merak/tool_meta.hpp`, `libs/loop/src/agent_loop.cpp`

### 1c. 电路断路器阈值可配置

**问题：** `kCircuitBreakerThreshold` 是 `static constexpr int = 3`，错误消息硬编码 "3"。

**方案：** 移入 `AgentLoop::Config`，错误消息动态构造。

```cpp
// libs/loop/include/merak/agent_loop.hpp Config 新增
int circuit_breaker_threshold = 3;
```

```cpp
// agent_loop.cpp 错误消息构造
blocked.output = "Tool '" + call.name + "' blocked (" +
    std::to_string(config_.circuit_breaker_threshold) +
    " consecutive failures). Try a different approach.";
```

删除 `kCircuitBreakerThreshold` constexpr。

**文件：** `libs/loop/include/merak/agent_loop.hpp`, `libs/loop/src/agent_loop.cpp`

### 1d. TurnGuard 自身加 mutex

**问题：** `TurnGuard::warning_count_` 无并发保护。`StallDetector::recent_rounds_` / `turn_counter_` 无并发保护。并发 `run()` / `resume()` 导致 data race。

**方案：** `TurnGuard` 和 `StallDetector` 各加 `std::mutex`，`evaluate()` / `reset()` / `check()` 入口加锁。

```cpp
// turn_guard.hpp 新增
private:
    mutable std::mutex mutex_;
```

```cpp
// stall_detector.hpp 新增
private:
    mutable std::mutex mutex_;
```

```cpp
// turn_guard.cpp evaluate() 入口
std::lock_guard<std::mutex> lock(mutex_);
```

```cpp
// stall_detector.cpp check() 入口
std::lock_guard<std::mutex> lock(mutex_);
```

**文件：** `libs/loop/include/merak/turn_guard.hpp`, `libs/loop/src/turn_guard.cpp`, `libs/loop/include/merak/stall_detector.hpp`, `libs/loop/src/stall_detector.cpp`

---

## Batch 2：子 Agent 隔离 & 生命周期

**修复：** 2 CRITICAL + 5 HIGH + 3 MEDIUM
**预估 diff：** ~250 行

### 2a. AgentTool spawn: detached thread → tracked future

**问题：** `agent_tool.cpp:97-104` 用 `std::thread(...).detach()` 启动子 Agent，结果丢弃，无取消路径，无并发限制。父 session 销毁后悬垂访问。

**方案：** 用 `std::async` 取代 `std::thread().detach()`，AgentTool 内部维护 `active_tasks_` map，新增 `get_result` action。

```cpp
// libs/tools/include/merak/agent_tool.hpp 新增成员
private:
    std::map<std::string, std::future<std::string>> active_tasks_;
    std::mutex tasks_mutex_;
```

```cpp
// agent_tool.cpp spawn action
auto fut = std::async(std::launch::async,
    [exec = executor_, agent_cfg = it->second, task_text]() -> std::string {
        try {
            NullRunControl control;
            return exec(agent_cfg, task_text, control);
        } catch (const std::exception& e) {
            spdlog::error("AgentTool: sub-agent failed: {}", e.what());
            return std::string("Error: ") + e.what();
        }
    });

std::string task_id = worldbuilding::make_id("task");
{
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    // 限制最大并发数
    if (active_tasks_.size() >= kMaxConcurrentSubAgents) {
        result.output = R"({"status":"error","message":"Too many concurrent sub-agents"})";
        result.is_error = true;
        return result;
    }
    active_tasks_[task_id] = std::move(fut);
}

nlohmann::json out;
out["status"] = "ok";
out["message"] = "Sub-agent spawned";
out["task_id"] = task_id;
out["agent_id"] = agent_id;
result.output = out.dump();
```

```cpp
// 新增 get_result action
else if (action == "get_result") {
    std::string task_id = json.value("task_id", "");
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    auto it = active_tasks_.find(task_id);
    if (it == active_tasks_.end()) {
        result.output = R"({"status":"error","message":"Unknown task_id"})";
        result.is_error = true;
    } else {
        auto status = it->second.wait_for(std::chrono::seconds(30));
        if (status == std::future_status::ready) {
            auto output = it->second.get();
            active_tasks_.erase(it);
            result.output = R"({"status":"ok","result":")" + output + R"("})";
        } else {
            result.output = R"({"status":"pending","message":"Task still running"})";
        }
    }
}
```

新增 `kMaxConcurrentSubAgents = 8` 常量。

**文件：** `libs/tools/include/merak/agent_tool.hpp`, `libs/tools/src/agent_tool.cpp`

### 2b. 子 Agent MemoryStore 隔离

**问题：** `sub_agent_runner.cpp:144-145` 所有子 Agent 共享父 `MemoryStore`，子 Agent 内部对话污染父 Agent 的 `working_memory_` 和语义搜索结果。

**方案：** 给每个子 Agent 创建独立的 `MemoryStore` 实例。

```cpp
// sub_agent_runner.cpp create_sub_agent()
auto sub_memory = std::make_shared<MemoryStore>(memory_config_, embedder_);
// 子 Agent 有独立的 working_memory_，语义搜索互不干扰
auto loop = std::make_unique<AgentLoop>(
    cfg, llm_, sub_tools, sub_memory, comp, worldbuilding_, skill_registry_);
```

`SubAgentRunner` 构造函数保存 `embedder_` 和 `memory_config_` 用于创建子 Agent 独立 MemoryStore：

```cpp
// sub_agent_runner.hpp 新增成员
std::shared_ptr<EmbeddingProvider> embedder_;
MemoryConfig memory_config_;
```

**文件：** `libs/loop/include/merak/sub_agent_runner.hpp`, `libs/loop/src/sub_agent_runner.cpp`

### 2c. SubAgentConfig 补全字段

**问题：** `SubAgentConfig::model` 被静默忽略，`max_turns` 无此字段无法覆盖硬编码的 10。

**方案：** 在 `SubAgentConfig` 新增 `max_turns`，`create_sub_agent()` 正确读取 `model`。

```cpp
// libs/config/include/merak/config.hpp SubAgentConfig 新增
int max_turns = 0;  // 0 = use default (10)
```

```cpp
// sub_agent_runner.cpp create_sub_agent()
cfg.max_turns = profile.max_turns > 0 ? profile.max_turns : 10;
if (!profile.model.empty()) {
    cfg.default_model = profile.model;
}
```

**文件：** `libs/config/include/merak/config.hpp`, `libs/loop/src/sub_agent_runner.cpp`

### 2d. fan_out 修复：死循环 + 部分失败丢失

**问题 1：** `hardware_concurrency()` 返回 0 时 `max_parallel = 0` 导致外层 while 死循环。

**修复：**
```cpp
// sub_agent_runner.cpp fan_out()
const int max_parallel = std::max(1, std::min(4,
    static_cast<int>(std::thread::hardware_concurrency())));
```

**问题 2：** 任一子 Agent 异常传播，所有已完成结果丢失。

**修复：**
```cpp
for (auto& f : batch) {
    try {
        auto [id, resp] = f.get();
        results[id] = resp;
    } catch (const std::exception& e) {
        AgentResponse err;
        err.text = std::string("Sub-agent error: ") + e.what();
        results[id + "_error"] = err;
    }
}
```

`sequential` 同理包装 try-catch，失败时在 accumulated 中追加错误标记并继续后续步骤。

**文件：** `libs/loop/src/sub_agent_runner.cpp`

### 2e. profiles_ 加读写锁

**问题：** `register_profile()` 写 `profiles_`，`delegate()` / `has_agent()` 读 `profiles_`，无同步保护。

**方案：**
```cpp
// sub_agent_runner.hpp 新增
mutable std::shared_mutex profiles_mutex_;
```

```cpp
// register_profile()
std::unique_lock<std::shared_mutex> lock(profiles_mutex_);
profiles_[config.id] = config;

// delegate() / has_agent()
std::shared_lock<std::shared_mutex> lock(profiles_mutex_);
auto it = profiles_.find(agent_id);
```

**文件：** `libs/loop/include/merak/sub_agent_runner.hpp`, `libs/loop/src/sub_agent_runner.cpp`

### 2f. SubAgentRunner 生命周期安全（std::async 捕获）

**问题：** `delegate()` / `fan_out()` / `sequential()` 中 `std::async` lambda 捕获裸 `this`。调用方在 future get 前销毁 runner 导致 use-after-free。

**方案：** `SubAgentRunner` 继承 `std::enable_shared_from_this`，lambda 捕获 `shared_from_this()`。

```cpp
// sub_agent_runner.hpp
class SubAgentRunner : public std::enable_shared_from_this<SubAgentRunner> {
```

```cpp
// delegate()
auto self = shared_from_this();
return std::async(std::launch::async,
    [self, agent_id, task]() -> AgentResponse {
        auto it = self->profiles_.find(agent_id);
        ...
    });
```

**文件：** `libs/loop/include/merak/sub_agent_runner.hpp`, `libs/loop/src/sub_agent_runner.cpp`

---

## Batch 3：可观测性 & API 完善

**修复：** 2 HIGH + 5 MEDIUM
**预估 diff：** ~200 行

### 3a. AgentLoop RunMetrics 结构体

**问题：** `IngestedTurn` 数据创建后仅 spdlog::debug 输出，`had_error` 写死 false，`llm_latency` 写死 0ms，`cache_read/write` 写死 0。

**方案：** 在 AgentLoop 内部维护 `RunMetrics`，在整个 run 生命周期中累积，通过 const 引用暴露。

```cpp
// libs/loop/include/merak/agent_loop.hpp 新增结构体
struct RunMetrics {
    int turns_completed = 0;
    int total_input_tokens = 0;
    int total_output_tokens = 0;
    int total_cache_read_tokens = 0;
    int total_cache_write_tokens = 0;
    int total_tool_calls = 0;
    int tool_errors = 0;
    int compactions_triggered = 0;
    int messages_compacted = 0;
    int circuit_breaker_trips = 0;
    int stall_force_stops = 0;
    int turn_guard_warnings = 0;
    std::chrono::milliseconds total_llm_latency{0};
};

// AgentLoop 新增公开方法
const RunMetrics& metrics() const { return run_metrics_; }
```

`run_loop()` 中填充逻辑：
- LLM 响应后：`run_metrics_.total_input_tokens += llm_response.total_input_tokens`，`run_metrics_.total_output_tokens += llm_response.total_output_tokens`，`run_metrics_.total_llm_latency += elapsed`
- 工具错误：`run_metrics_.tool_errors++`
- 断路器触发：`run_metrics_.circuit_breaker_trips++`
- Stall ForceStop：`run_metrics_.stall_force_stops++`
- TurnGuard Warning：`run_metrics_.turn_guard_warnings++`

**修复 TurnIngestor 数据填充：**
- `turn_ingestor.cpp:16`：`had_error` 改为根据 tool result `is_error` 字段判断
- `agent_loop.cpp:171`：`cache_read`/`cache_write` 从实际 provider response 取值
- `agent_loop.cpp:173`：`llm_latency` 用实际 LLM 调用耗时而非 `std::chrono::milliseconds{0}`

**文件：** `libs/loop/include/merak/agent_loop.hpp`, `libs/loop/src/agent_loop.cpp`, `libs/loop/src/turn_ingestor.cpp`

### 3b. SSE 流持续循环 + 完整事件类型

**问题：** SSE handler 退出一轮后关闭；跳过 `message_appended` 和 `compaction_applied` 事件类型。

**方案：** 改为持续循环，keep-alive 心跳，不再过滤事件类型。

```cpp
// http_server.cpp SSE handler 改为持续循环
server_.set_chunked_content_provider(sse_path, [&](size_t offset, httplib::DataSink& sink) {
    auto sub = runtime_->subscribe(session_id);
    while (!sink.is_writable()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    while (!cancelled) {
        auto events = runtime_->drain_events(session_id);
        for (auto& e : events) {
            // 不再过滤 message_appended / compaction_applied
            sink.write(e.format_sse().data(), e.format_sse().size());
        }
        // keep-alive
        sink.write(": heartbeat\n\n", 15);
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
});
```

**文件：** `libs/http/src/http_server.cpp`

### 3c. 补齐 REST 端点

**问题：** 缺 `GET /v1/runs`（list）、`GET /v1/runs/:id/output`（取文本）、`POST /v1/runs/:id/resume`（恢复）。

**方案：** 新增三个路由：

```
GET /v1/runs?session_id=<sid>&status=<running|completed|error>&limit=20&offset=0
  → 200 { "runs": [ { "id", "session_id", "status", "created_at", "turn_count" } ], "total": N }

GET /v1/runs/:id/output
  → 200 { "text": "...", "turn_count": N, "status": "running|complete" }
  → 404 { "error": "run not found" }

POST /v1/runs/:id/resume
  → 200 { "run_id": "<new_run_id>", "status": "resumed" }
  → 409 { "error": "run is not resumable — must be in interrupted/error state" }
```

实现：
- `GET /v1/runs` — 从 `SessionStore` 查询 runs 表，支持过滤和分页
- `GET /v1/runs/:id/output` — 从 `RuntimeService` 取该 run 的累积文本和状态
- `POST /v1/runs/:id/resume` — 读取最新 checkpoint，构造新 AgentLoop，调用 `restore_history()` + `resume()`

**文件：** `libs/http/src/http_server.cpp`, `libs/runtime/include/merak/runtime_service.hpp`, `libs/runtime/src/runtime_service.cpp`

### 3d. NullRunControl 加 warning log

**问题：** `NullRunControl` 静默丢弃所有遥测，自动批准所有护栏操作。

**方案：** 构造函数加 warning log，不改功能行为（它有合理的测试/子 Agent 使用场景）。

```cpp
// execution.hpp NullRunControl 构造函数
NullRunControl() : token_(std::make_shared<CancellationToken>()) {
    spdlog::warn("NullRunControl: sub-agent observability disabled, "
                 "all tool approvals auto-granted, no cancellation support");
}
```

**文件：** `libs/core/include/merak/execution.hpp`

### 3e. HTTP 基础防护

**问题：** 无请求体大小限制、无 rate limiting、无输入校验。

**方案：** 在 HTTP server 配置中增加限制，pre-routing handler 中执行。

```cpp
// libs/http/include/merak/http_server.hpp 新增
struct HttpLimits {
    size_t max_body_size = 10 * 1024 * 1024;  // 10MB
    int max_requests_per_minute = 120;
    size_t max_field_length = 65536;          // 单个字符串字段上限
};
```

```cpp
// http_server.cpp pre_routing handler
server_.set_pre_routing_handler([&](const httplib::Request& req, httplib::Response& res) {
    // Body size check
    if (req.body.size() > limits_.max_body_size) {
        res.status = 413;
        res.set_content(R"({"error":"payload too large"})", "application/json");
        return httplib::Server::HandlerResponse::Unhandled;
    }
    // Simple per-IP rate limit (sliding window)
    auto client_ip = req.remote_addr;
    if (!rate_limiter_->allow(client_ip)) {
        res.status = 429;
        res.set_content(R"({"error":"rate limit exceeded"})", "application/json");
        return httplib::Server::HandlerResponse::Unhandled;
    }
    return httplib::Server::HandlerResponse::Unhandled;
});
```

Rate limiter 用简单的 token bucket（per-IP，60 秒窗口，120 请求），作为 `HttpServer` 内部工具类。

**文件：** `libs/http/include/merak/http_server.hpp`, `libs/http/src/http_server.cpp`

---

## Batch 4：工具系统 & 上下文加固

**修复：** 1 CRITICAL + 3 HIGH + 4 MEDIUM
**预估 diff：** ~180 行

### 4a. 工具执行超时

**问题：** `Tool::execute()` 无超时参数，挂起的工具永久阻塞循环。

**方案：** `ToolExecutionContext` 增加 `timeout` 字段，`handle_tool_calls()` 用 `wait_for` 执行。

```cpp
// libs/core/include/merak/execution.hpp ToolExecutionContext 新增
std::chrono::milliseconds timeout{30000};  // 默认 30s
```

```cpp
// agent_loop.cpp handle_tool_calls() 中
auto timeout_dur = ctx.timeout;  // 必须在 std::move(ctx) 前捕获
auto result_future = tools_->execute(call, std::move(ctx));
auto status = result_future.wait_for(timeout_dur);
if (status == std::future_status::timeout) {
    // cancellation is copied into execute() via ToolExecutionContext — still valid
    ToolResult timeout_result;
    timeout_result.call_id = call.id;
    timeout_result.is_error = true;
    timeout_result.output = "Tool '" + call.name + "' timed out after " +
        std::to_string(timeout_dur.count()) + "ms";
    results.push_back(timeout_result);
    control.emit_tool_completed(call, timeout_result);
    continue;
}
auto result = result_future.get();
```

`AgentLoop::Config` 新增 `int tool_timeout_ms = 30000`。

**文件：** `libs/core/include/merak/execution.hpp`, `libs/loop/include/merak/agent_loop.hpp`, `libs/loop/src/agent_loop.cpp`

### 4b. JSON Schema 参数校验

**问题：** `ToolRegistry::execute()` 不校验参数，LLM 幻觉的参数直接传入工具实现。

**方案：** 执行前做 lightweight JSON 校验（required 字段存在 + 类型匹配）。

```cpp
// tool_registry.cpp execute() 新增校验步骤
auto spec = find_spec(call.name);
if (spec && !spec->parameters_json.empty()) {
    auto result = validate_arguments(call.arguments, spec->parameters_json);
    if (!result.ok) {
        ToolResult invalid;
        invalid.call_id = call.id;
        invalid.is_error = true;
        invalid.output = "Invalid arguments for '" + call.name + "': " + result.error;
        return std::async(std::launch::deferred,
            [r = std::move(invalid)]() mutable { return std::move(r); });
    }
}
```

```cpp
// tool_registry.cpp 匿名 namespace 新增（对齐现有 match_score 的 convention，line 155）
namespace {
struct ValidationResult { bool ok; std::string error; };

ValidationResult validate_arguments(
    const std::string& args_json,
    const std::string& schema_json)
{
    try {
        auto schema = nlohmann::json::parse(schema_json);
        auto args = nlohmann::json::parse(args_json);
        if (!args.is_object()) {
            return {false, "arguments must be a JSON object"};
        }
        // Check required fields
        if (schema.contains("required") && schema["required"].is_array()) {
            for (auto& req : schema["required"]) {
                if (!args.contains(req.get<std::string>())) {
                    return {false, "missing required field: " + req.get<std::string>()};
                }
            }
        }
        // Check type constraints on properties
        if (schema.contains("properties") && schema["properties"].is_object()) {
            for (auto& [key, prop] : schema["properties"].items()) {
                if (!args.contains(key)) continue;
                auto& val = args[key];
                if (prop.contains("type")) {
                    std::string expected = prop["type"].get<std::string>();
                    bool type_ok = false;
                    if (expected == "string") type_ok = val.is_string();
                    else if (expected == "number" || expected == "integer") type_ok = val.is_number();
                    else if (expected == "boolean") type_ok = val.is_boolean();
                    else if (expected == "array") type_ok = val.is_array();
                    else if (expected == "object") type_ok = val.is_object();
                    if (!type_ok) {
                        return {false, "field '" + key + "' expected type " + expected};
                    }
                }
            }
        }
        return {true, ""};
    } catch (const nlohmann::json::exception& e) {
        return {false, std::string("JSON parse error: ") + e.what()};
    }
}

} // namespace
```

**文件：** `libs/tools/include/merak/tool_registry.hpp`, `libs/tools/src/tool_registry.cpp`

### 4c. Compactor 异常容错

**问题：** `maybe_compact()` 和 `ContextOptimizer::drop_rounds()` 中 LLM 异常无 try-catch，传播崩溃整个 run。

**方案：** 包装在 try-catch 中，失败时降级继续。

```cpp
// agent_loop.cpp maybe_compact()
if (total_tokens > config_.model_max_tokens * 0.75 && compactor_) {
    try {
        auto result = compactor_->compact_history(session_history_, keep_recent).get();
        if (!result.summary.empty()) {
            Message summary_msg;
            summary_msg.role = "system";
            summary_msg.content = "[Previous conversation summary]\n" + result.summary;
            compaction_summaries_.push_back(summary_msg);
            control.record_compaction(static_cast<int>(result.replaced.size()));
            run_metrics_.compactions_triggered++;
            run_metrics_.messages_compacted += static_cast<int>(result.replaced.size());
        }
    } catch (const std::exception& e) {
        spdlog::warn("Compaction failed, continuing without summary: {}", e.what());
    }
}
```

`ContextOptimizer::drop_rounds()` 同理：每个 future 独立的 try-catch。

```cpp
// context_optimizer.cpp drop_rounds()
for (size_t i = 0; i < futures.size(); i++) {
    try {
        auto summary = futures[i].get();
        // 现有处理逻辑
    } catch (const std::exception& e) {
        spdlog::warn("Microcompaction round {} failed: {}", i, e.what());
    }
}
```

**文件：** `libs/loop/src/agent_loop.cpp`, `libs/context/src/context_optimizer.cpp`

### 4d. Token 预算硬执行

**问题：** `planned_assemble()` 计算 `tokens_after` 但不对标 `model_max_tokens` 做强裁剪。

**方案：** 在序列化前增加第二轮裁剪步骤。

```cpp
// context_pipeline.cpp planned_assemble() 末尾
if (opt_stats.tokens_after > model_max_tokens) {
    auto& msgs = serializer_.mutable_messages();
    // 从头部去掉最旧的非 system 消息，直到在预算内
    int removed = 0;
    while (opt_stats.tokens_after > model_max_tokens && msgs.size() > 2) {
        opt_stats.tokens_after -= estimate_tokens(msgs[1].content);
        msgs.erase(msgs.begin() + 1);
        removed++;
    }
    stats_.hard_trims += removed;
    spdlog::warn("ContextPipeline: hard trim removed {} messages to fit budget", removed);
}
```

**文件：** `libs/context/include/merak/context_pipeline.hpp`, `libs/context/include/merak/pipeline_stats.hpp`, `libs/context/src/context_pipeline.cpp`

### 4e. O(1) 缓存的用户查询

**问题：** `build_context()` 每轮 O(n) 反向扫描 `session_history_` 找最近 user 消息。

**方案：** 成员变量缓存，O(1) 返回。

```cpp
// agent_loop.hpp 新增成员
std::string last_user_query_;

// agent_loop.cpp run() 中
last_user_query_ = user_message;

// build_context() 中
sources.search_query = last_user_query_;
```

**文件：** `libs/loop/include/merak/agent_loop.hpp`, `libs/loop/src/agent_loop.cpp`

### 4f. 工具调用频率限制

**问题：** 无机制限制工具调用速率，LLM 可无限调用。

**方案：** 引入基础硬上限。

```cpp
// libs/loop/include/merak/agent_loop.hpp — AgentLoop::Config 内部新增
struct ToolRateLimit {
    int max_calls_per_turn = 50;
    int max_calls_per_run = 500;
};
ToolRateLimit tool_rate_limit;
```
注意：`ToolRateLimit` 放在 `agent_loop.hpp` 而非 `tool_registry.hpp`。限流是循环级策略（与断路器同类），执行点也在 `handle_tool_calls()` 中，放在 AgentLoop 层是正确的分层归属。

```cpp
// agent_loop.cpp handle_tool_calls() 中
for (auto& call : calls) {
    run_call_count_++;
    if (run_call_count_ > config_.tool_rate_limit.max_calls_per_run) {
        ToolResult limited;
        limited.call_id = call.id;
        limited.is_error = true;
        limited.output = "Tool call limit exceeded (" +
            std::to_string(config_.tool_rate_limit.max_calls_per_run) +
            " per run).";
        results.push_back(limited);
        continue;
    }
    turn_call_count_++;
    if (turn_call_count_ > config_.tool_rate_limit.max_calls_per_turn) {
        // 本轮内不再执行更多工具，将剩余 calls 标记为跳过的
        ToolResult skipped;
        skipped.call_id = call.id;
        skipped.is_error = true;
        skipped.output = "Skipped: turn tool call limit reached.";
        results.push_back(skipped);
        continue;
    }
    // ... 正常执行
}
```

每轮开始时 `turn_call_count_` 归零。

**文件：** `libs/loop/include/merak/agent_loop.hpp`, `libs/loop/src/agent_loop.cpp`

### 4g. ContextPipeline 线程安全

**问题：** `planned_assemble()` 无同步保护。

**方案：** 加 `std::mutex` 保护可变状态。

```cpp
// context_pipeline.hpp 新增
private:
    mutable std::mutex mutex_;

// context_pipeline.cpp planned_assemble() 入口
std::lock_guard<std::mutex> lock(mutex_);
```

**文件：** `libs/context/include/merak/context_pipeline.hpp`, `libs/context/src/context_pipeline.cpp`

---

## 测试策略

### 每批测试用例

| 批次 | 测试文件 | 测试内容 |
|------|----------|----------|
| B1 | `test_turn_guard.cpp`（新增） | TurnGuardConfig 自定义阈值生效、Nudge 自定义文本、边界值 |
| B1 | `test_agent_loop.cpp`（扩） | ToolCategory 查询正确、circuit_breaker 自定义阈值 |
| B2 | `test_sub_agent_runner.cpp`（扩） | MemoryStore 隔离验证、fan_out 异常容错、max_turns 覆盖、model 字段生效 |
| B2 | `test_agent_tool.cpp`（新增） | spawn + get_result 流程、并发数限制、task_id 唯一性 |
| B3 | `test_http.cpp`（扩） | GET /v1/runs list、GET /v1/runs/:id/output、POST resume、413/429 响应 |
| B4 | `test_tool_registry.cpp`（新增） | Schema 校验 — 缺 required 字段、类型错误、合法参数 |
| B4 | 无新文件 | 超时、Compactor 容错、频率限制 在 `test_agent_loop.cpp` 扩 |

### 回归风险

- B1：TurnGuardConfig 默认值 = 现有值 → 无行为变更
- B2：子 Agent MemoryStore 隔离改变内存使用模式 → 需关注内存增量
- B3：SSE 循环改变连接生命周期 → 需测试客户端重连
- B4：Schema 校验可能暴露之前被静默忽略的无效参数 → 需检查各工具 schema 是否准确

---

## 不修的问题

以下 LOW 级别问题留作后续迭代：

- `stall_detector.cpp:49-50` `stalled_sig` 只取 index 0（多工具 stall 信息丢失）
- `stall_detector.cpp:12-14` 非确定性参数（timestamp/UUID）击败哈希
- 工具结果框架级缓存 TTL
- 子 Agent 间消息传递机制（blackboard pattern）

---

## 文件总表

| 文件 | B1 | B2 | B3 | B4 |
|------|:--:|:--:|:--:|:--:|
| `libs/core/include/merak/execution.hpp` | | | ✓ | ✓ |
| `libs/core/include/merak/tool_meta.hpp` | ✓ | | | |
| `libs/config/include/merak/config.hpp` | | ✓ | | |
| `libs/loop/include/merak/agent_loop.hpp` | ✓ | | ✓ | ✓ |
| `libs/loop/src/agent_loop.cpp` | ✓ | | ✓ | ✓ |
| `libs/loop/include/merak/turn_guard.hpp` | ✓ | | | |
| `libs/loop/src/turn_guard.cpp` | ✓ | | | |
| `libs/loop/include/merak/stall_detector.hpp` | ✓ | | | |
| `libs/loop/src/stall_detector.cpp` | ✓ | | | |
| `libs/loop/src/turn_ingestor.cpp` | | | ✓ | |
| `libs/loop/include/merak/sub_agent_runner.hpp` | | ✓ | | |
| `libs/loop/src/sub_agent_runner.cpp` | | ✓ | | |
| `libs/tools/include/merak/agent_tool.hpp` | | ✓ | | |
| `libs/tools/src/agent_tool.cpp` | | ✓ | | |
| `libs/tools/include/merak/tool_registry.hpp` | ✓ | | | ✓ |
| `libs/tools/src/tool_registry.cpp` | ✓ | | | ✓ |
| `libs/http/include/merak/http_server.hpp` | | | ✓ | |
| `libs/http/src/http_server.cpp` | | | ✓ | |
| `libs/runtime/include/merak/runtime_service.hpp` | | | ✓ | |
| `libs/runtime/src/runtime_service.cpp` | | | ✓ | |
| `libs/context/include/merak/context_pipeline.hpp` | | | | ✓ |
| `libs/context/src/context_pipeline.cpp` | | | | ✓ |
| `libs/context/src/context_optimizer.cpp` | | | | ✓ |
| `libs/context/include/merak/pipeline_stats.hpp` | | | | ✓ |
