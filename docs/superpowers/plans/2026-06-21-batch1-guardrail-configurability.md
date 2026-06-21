# Plan: Batch 1 — 护栏可配置化

**日期:** 2026-06-21
**设计文档:** `docs/superpowers/specs/2026-06-21-agent-industrial-hardening-design.md`
**目标分支:** `infra-fixes-2026-06-20`
**预计 diff:** ~150 行

---

## 1. 需求重述

将 TurnGuard 和 AgentLoop 中硬编码的阈值、Nudge 消息、工具名列表、电路断路器参数全部转为可配置。消除并发 data race。

4 个子任务：
- **1a** TurnGuardConfig 结构体 — 6 阈值 + 6 Nudge 消息可配置
- **1b** ToolDomain 标志位 — 消除 agent_loop.cpp 中两处硬编码工具名字符串列表
- **1c** 电路断路器阈值可配置 — constexpr → AgentLoop::Config
- **1d** TurnGuard / StallDetector 加 mutex — 消除并发 data race

---

## 2. 当前状态

| 文件 | 硬编码位置 | 硬编码内容 |
|------|-----------|-----------|
| `turn_guard.cpp:6` | `if (count >= 4) return -999` | 惩罚阈值 |
| `turn_guard.cpp:19` | `>= 5` | world-query 轮数上限 |
| `turn_guard.cpp:27` | `>= 3` | read-only 轮数上限 |
| `turn_guard.cpp:33` | `>= 3` | content avoidance 上限 |
| `turn_guard.cpp:39` | `>= 15` | 单轮工具调用上限 |
| `turn_guard.cpp:30,36,47,52,57` | 硬编码中文消息 | 5 条 nudge |
| `turn_guard.cpp:65` | `>= 4` | warning 上限 |
| `agent_loop.cpp:301-316` | 硬编码工具名字符串 | write tools + query tools 列表 |
| `agent_loop.hpp:110` | `static constexpr int kCircuitBreakerThreshold = 3` | 断路器阈值 |
| `agent_loop.cpp:508-509` | 硬编码 "3" 在消息中 | 错误消息 stale literal |
| 无 mutex | turn_guard.hpp:42, stall_detector.hpp:51-53 | data race |

---

## 3. 实施步骤

### Step 1: ToolDomain 枚举 + ToolMeta 字段 (1b 前半)

**文件:** `libs/core/include/merak/tool_meta.hpp`
**依赖:** 无
**预计改动:** +15 行

在 `tool_meta.hpp` 中（`IntentType` 枚举之后、`ToolMeta` 结构体之前）新增：

```cpp
enum class ToolDomain : uint8_t {
    General    = 0,
    Write      = 1 << 0,
    WorldQuery = 1 << 1,
};
```

在 `ToolMeta` 结构体中新增字段（最后一行 `schema_tokens` 之后）：

```cpp
ToolDomain domain = ToolDomain::General;
```

**验证:** 编译通过（`General = 0` 保证默认值与默认初始化行为一致）。

---

### Step 2: ToolRegistry 新增 domain_of() + domains_ map (1b 中段)

**文件:** `libs/tools/include/merak/tool_registry.hpp`, `libs/tools/src/tool_registry.cpp`
**依赖:** Step 1
**预计改动:** +10 行

`tool_registry.hpp` 新增公开方法和私有成员：

```cpp
// 公开方法（在 find_spec 之后、get_tool 之前）
ToolDomain domain_of(const std::string& name) const {
    auto it = domains_.find(name);
    return it != domains_.end() ? it->second : ToolDomain::General;
}

// 私有成员（在 source_ 之后）
std::map<std::string, ToolDomain> domains_;
```

`tool_registry.cpp` `register_tool()` 新增 2 行（在 `source_[name] = spec.source;` 之后）：

```cpp
auto meta = tool->meta();
domains_[name] = meta.domain;
```

**验证:** 编译通过。现有测试不受影响（现有的 `register_tool` 调用继续工作，`domains_` 自动填充）。

---

### Step 3: 各工具 meta() 设置 ToolDomain (1b 后半)

**文件:** 多个工具实现文件
**依赖:** Step 2
**预计改动:** ~40 行（~18 个文件中各 1-2 行）

以表格为准设置 `m.domain`：

| 工具文件 | 工具名 | 设置的 domain |
|----------|--------|-------------|
| `fs_tools.cpp` | `read_file` | `ToolDomain::General` |
| `fs_tools.cpp` | `write_file` | `ToolDomain::Write` |
| `fs_tools.cpp` | `str_replace` | `ToolDomain::Write` |
| `shell_tool.cpp` | `execute_bash` | `ToolDomain::General` |
| `worldbuilding_tools.cpp` | `create_character`, `create_scene`, `create_chapter` | `ToolDomain::Write` |
| `worldbuilding_tools.cpp` | `create_location`, `add_world_knowledge` | `ToolDomain::Write` |
| `worldbuilding_tools.cpp` | `plant_foreshadowing`, `expose_secret` | `ToolDomain::Write` |
| `worldbuilding_tools.cpp` | `advance_world_time` 等所有 `IntentType::DomainWrite` 工具 | `ToolDomain::Write` |
| `worldbuilding_tools.cpp` | `query_map`, `query_world`, `query_history` | `ToolDomain::WorldQuery` |
| `worldbuilding_tools.cpp` | `query_magic`, `query_faction` | `ToolDomain::WorldQuery` |
| `worldbuilding_tools.cpp` | `search_agent`, `look_around` | `ToolDomain::WorldQuery` |
| `worldbuilding_tools.cpp` | `read_character_card`, `read_secret` | `ToolDomain::WorldQuery` |
| `worldbuilding_tools.cpp` | `read_foreshadowing`, `search_my_diary` | `ToolDomain::WorldQuery` |
| `worldbuilding_tools.cpp` | 所有 `IntentType::DomainRead` 工具 | `ToolDomain::WorldQuery` |
| `git_tool.cpp` | git tools | `ToolDomain::General` |
| `web_fetch_tool.cpp` | `web_fetch` | `ToolDomain::General` |
| `web_search_tool.cpp` | `web_search` | `ToolDomain::General` |
| `lsp_tool.cpp` | lsp tools | `ToolDomain::General` |
| `symbols_tool.cpp` | symbols tools | `ToolDomain::General` |
| `memory_tool.cpp` | memory tools | `ToolDomain::General` |
| `session_tool.cpp` | session tools | `ToolDomain::General` |
| `agent_tool.cpp` | `agent` | `ToolDomain::General` |
| `task_tool.cpp` | task tools | `ToolDomain::General` |
| `ask_user_tool.cpp` | `ask_user` | `ToolDomain::General` |
| `tool_search_tool.cpp` | `tool_search` | `ToolDomain::General` |
| `plan_mode_tools.cpp` | `enter_plan_mode`, `exit_plan_mode` | `ToolDomain::General` |

每个工具改动模式：在 `meta()` 方法的 `return m;` 前加一行 `m.domain = ToolDomain::XXX;`。

对于 WorldBuilding 工具，可以用 `IntentType::DomainWrite` 已有分类做参照：所有 `DomainWrite` 工具 → `ToolDomain::Write`，所有 `DomainRead` 工具 → `ToolDomain::WorldQuery`（不含同时有 `Domain::Write` 的工具）。

**验证:** 编译通过。所有工具继续返回完整的 ToolMeta。

---

### Step 4: agent_loop.cpp 替换硬编码工具名列表 (1b 完成)

**文件:** `libs/loop/src/agent_loop.cpp`
**依赖:** Step 3
**预计改动:** -26 行 +10 行

**删除** lines 298-316（硬编码的 write/query 工具名检测逻辑），替换为：

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

删除后的 guard_in.had_write_operation、consecutive_read_only_rounds_ / consecutive_world_query_rounds_ 计数器逻辑不变。

**验证:** 现有 `test_agent_loop.cpp` 测试通过（TurnGuard 行为不变，只是分类方式从字符串匹配改为标志位查询）。

---

### Step 5: TurnGuardConfig 结构体 + TurnGuard 构造函数改造 (1a)

**文件:** `libs/loop/include/merak/turn_guard.hpp`, `libs/loop/src/turn_guard.cpp`
**依赖:** 无（可独立做，但建议在 Step 4 之后）
**预计改动:** +25 行，修改 10 行

`turn_guard.hpp` 中 `TurnGuard` 类定义前新增：

```cpp
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

`TurnGuard` 类改动：
- 新增 `explicit TurnGuard(TurnGuardConfig cfg = {}) : config_(std::move(cfg)) {}`
- 新增私有成员 `TurnGuardConfig config_;`
- `penalty_for()` 方法签名为 `int penalty_for(int count) const;`

`turn_guard.cpp` 改动：将所有 magic number 替换为 `config_.xxx`：

| 原代码 | 替换为 |
|--------|--------|
| `count >= 4` (line 6) | `count >= config_.max_warnings_before_critical` |
| `return -(2 * count)` (line 7) | 不变 |
| `in.consecutive_world_query_rounds >= 5` | `>= config_.max_consecutive_world_query_rounds` |
| `in.consecutive_read_only_rounds >= 3` | `>= config_.max_consecutive_read_only_rounds` |
| `in.consecutive_content_avoidance >= 3` | `>= config_.max_consecutive_content_avoidance` |
| `in.tool_count >= 15` | `>= config_.max_tool_calls_per_round` |
| `warning_count_ >= 4` (line 65) | `>= config_.max_warnings_before_critical` |
| 5 条硬编码中文 nudge | `config_.nudge_write_now` 等 |
| `"[校正] "` | `config_.nudge_prefix` |

**验证:** 默认构造 `TurnGuard{}` 行为与当前完全一致。

---

### Step 6: 电路断路器阈值可配置 (1c)

**文件:** `libs/loop/include/merak/agent_loop.hpp`, `libs/loop/src/agent_loop.cpp`
**依赖:** 无
**预计改动:** +1 行，修改 3 行

`agent_loop.hpp` `Config` 结构体新增：

```cpp
int circuit_breaker_threshold = 3;
```

删除 line 110 的 `static constexpr int kCircuitBreakerThreshold = 3;`。

`agent_loop.cpp` 中：

```cpp
// line 503: kCircuitBreakerThreshold → config_.circuit_breaker_threshold
if (it != tool_failure_streak_.end() && it->second >= config_.circuit_breaker_threshold) {

// lines 508-509: 动态构造错误消息
blocked.output = "Tool '" + call.name + "' blocked (" +
    std::to_string(config_.circuit_breaker_threshold) +
    " consecutive failures). Try a different approach.";
```

**验证:** 默认值 3 = 原行为。`test_agent_loop.cpp` 通过。

---

### Step 7: TurnGuard + StallDetector 加 mutex (1d)

**文件:** `libs/loop/include/merak/turn_guard.hpp`, `libs/loop/src/turn_guard.cpp`, `libs/loop/include/merak/stall_detector.hpp`, `libs/loop/src/stall_detector.cpp`
**依赖:** 无（可独立做）
**预计改动:** +12 行

`turn_guard.hpp` `TurnGuard` 私有成员新增：

```cpp
mutable std::mutex mutex_;
```

`turn_guard.cpp` 两个公开方法入口加锁：

```cpp
TurnGuard::Verdict TurnGuard::evaluate(const RoundInput& in) {
    std::lock_guard<std::mutex> lock(mutex_);
    // ... 现有逻辑不变
}

void TurnGuard::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    warning_count_ = 0;
}
```

`stall_detector.hpp` `StallDetector` 私有成员新增：

```cpp
mutable std::mutex mutex_;
```

`stall_detector.cpp` 两个公开方法入口加锁：

```cpp
StallResult StallDetector::check(const std::vector<ToolCall>& current_round) {
    std::lock_guard<std::mutex> lock(mutex_);
    // ... 现有逻辑不变
}

void StallDetector::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    recent_rounds_.clear();
    turn_counter_ = 0;
}
```

**验证:** 编译通过。单线程行为不变。`test_agent_loop.cpp` 通过。

---

### Step 8: 测试

**文件:** `libs/loop/tests/test_turn_guard.cpp`（新增）, `libs/loop/tests/test_agent_loop.cpp`（扩）
**依赖:** Step 5, Step 7
**预计改动:** ~50 行

`test_turn_guard.cpp` 新增测试：

```cpp
// 1. 默认 TurnGuardConfig 行为与硬编码值一致
TEST(TurnGuard, DefaultConfigMatchesHardcodedThresholds) {
    TurnGuard guard;
    TurnGuard::RoundInput in;
    in.consecutive_read_only_rounds = 3;
    auto v = guard.evaluate(in);
    EXPECT_GE(v.severity, Severity::Warning);
}

// 2. 自定义阈值生效
TEST(TurnGuard, CustomThresholdsTakeEffect) {
    TurnGuardConfig cfg;
    cfg.max_consecutive_read_only_rounds = 10;
    TurnGuard guard(cfg);
    TurnGuard::RoundInput in;
    in.consecutive_read_only_rounds = 3;
    auto v = guard.evaluate(in);
    EXPECT_EQ(v.severity, Severity::Healthy);  // 3 < 10, no warning
}

// 3. 自定义 Nudge 消息生效
TEST(TurnGuard, CustomNudgeMessages) {
    TurnGuardConfig cfg;
    cfg.nudge_write_now = "CUSTOM: write now";
    TurnGuard guard(cfg);
    TurnGuard::RoundInput in;
    in.consecutive_read_only_rounds = 3;
    auto v = guard.evaluate(in);
    ASSERT_TRUE(v.nudge.has_value());
    EXPECT_EQ(*v.nudge, "CUSTOM: write now");
}

// 4. Nudge prefix 自定义
TEST(TurnGuard, CustomNudgePrefix) {
    TurnGuardConfig cfg;
    cfg.nudge_prefix = "[HINT] ";
    TurnGuard guard(cfg);
    // ... 验证 prefix 生效
}
```

`test_agent_loop.cpp` 扩展：

```cpp
// 5. ToolDomain 查询正确
TEST(AgentLoop, ToolDomainClassification) {
    // 注册一个 Write 工具 + 一个 WorldQuery 工具
    // 验证 had_write / had_world_query_only 计算正确
}

// 6. 自定义 circuit_breaker_threshold 生效
TEST(AgentLoop, CustomCircuitBreakerThreshold) {
    AgentLoop::Config cfg;
    cfg.circuit_breaker_threshold = 1;
    // 验证单次失败即触发断路器
}
```

---

## 4. 依赖关系

```
Step 1 (ToolMeta + enum) 
  └→ Step 2 (ToolRegistry domains_)
       └→ Step 3 (各工具设置 domain)
            └→ Step 4 (agent_loop 替换硬编码)
Step 5 (TurnGuardConfig) — 独立
Step 6 (CircuitBreaker) — 独立
Step 7 (Mutex) — 独立
Step 8 (Tests) → 依赖 5, 6, 7 完成
```

Steps 5、6、7 可以做并行（无互相依赖）。Step 1-4 是链式依赖必须按序。

推荐执行顺序：1 → 2 → 3 → 4 → 5 → 6 → 7 → 8

---

## 5. 风险

| 风险 | 等级 | 缓解 |
|------|------|------|
| WorldBuilding 工具 ~40 个逐一修改 meta()，遗漏一个导致该工具 domain 为 General | MEDIUM | grep `IntentType::DomainRead`/`DomainWrite` 做对照，确保覆盖率 |
| `domain_of()` 查不到工具名时返回 `ToolDomain::General`，若拼写错误静默分类错误 | LOW | 现有 `find_spec` 有类似的 not-found 行为，保持一致 |
| TurnGuardConfig 默认值若与硬编码值不一致，行为变更 | LOW | 代码 review 逐项核对 |
| mutex 引入可能导致性能退化（evaluate 每轮调用一次） | NEGLIGIBLE | 临界区只有 ~50 行整数比较，锁竞争极小 |

---

## 6. 文件总表

| 文件 | Step | 改动类型 |
|------|------|----------|
| `libs/core/include/merak/tool_meta.hpp` | 1 | 新增 enum + 字段 |
| `libs/tools/include/merak/tool_registry.hpp` | 2 | 新增方法 + 字段 |
| `libs/tools/src/tool_registry.cpp` | 2 | modify register_tool |
| `libs/tools/src/fs_tools.cpp` | 3 | meta() 加 domain |
| `libs/tools/src/shell_tool.cpp` | 3 | meta() 加 domain |
| `libs/tools/src/git_tool.cpp` | 3 | meta() 加 domain |
| `libs/tools/src/web_fetch_tool.cpp` | 3 | meta() 加 domain |
| `libs/tools/src/web_search_tool.cpp` | 3 | meta() 加 domain |
| `libs/tools/src/lsp_tool.cpp` | 3 | meta() 加 domain |
| `libs/tools/src/symbols_tool.cpp` | 3 | meta() 加 domain |
| `libs/tools/src/memory_tool.cpp` | 3 | meta() 加 domain |
| `libs/tools/src/session_tool.cpp` | 3 | meta() 加 domain |
| `libs/tools/src/agent_tool.cpp` | 3 | meta() 加 domain |
| `libs/tools/src/task_tool.cpp` | 3 | meta() 加 domain |
| `libs/tools/src/ask_user_tool.cpp` | 3 | meta() 加 domain |
| `libs/tools/src/tool_search_tool.cpp` | 3 | meta() 加 domain |
| `libs/tools/src/plan_mode_tools.cpp` | 3 | meta() 加 domain |
| `libs/worldbuilding/src/worldbuilding_tools.cpp` | 3 | ~40 个工具 meta() 加 domain |
| `libs/loop/src/agent_loop.cpp` | 4, 6 | 替换工具名列表 + 断路器阈值 |
| `libs/loop/include/merak/agent_loop.hpp` | 6 | Config 加字段 |
| `libs/loop/include/merak/turn_guard.hpp` | 5, 7 | Config 结构体 + mutex |
| `libs/loop/src/turn_guard.cpp` | 5, 7 | 使用 Config + lock |
| `libs/loop/include/merak/stall_detector.hpp` | 7 | mutex |
| `libs/loop/src/stall_detector.cpp` | 7 | lock |
| `libs/loop/tests/test_turn_guard.cpp` | 8 | 新增测试 |
| `libs/loop/tests/test_agent_loop.cpp` | 8 | 扩展测试 |
