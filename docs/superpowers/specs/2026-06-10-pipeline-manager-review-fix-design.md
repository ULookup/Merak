# PipelineManager Review 修复设计

> 对 2026-06-09 实现进行严格 Review 后，发现 10 CRITICAL + 13 IMPORTANT + 8 MINOR 问题，本文档给出完整修复方案。

## 1. `all_checks_passed` 架构修复

**问题：** `eval_all_checks_passed` 是自由函数，无法访问 `ConditionEvaluator::check_registry_`，永远返回 `met=false`。reflection 阶段永远不能自动推进。PipelineManager 也未按设计文档实现拦截展开逻辑。

**方案：** `ConditionEvaluator::evaluate()` 内部截获 `all_checks_passed` 类型，直接访问 `this->check_registry_` 展开子检查。调用方无感知。

```cpp
// condition_evaluator.cpp — evaluate() 方法内新增分支
ConditionResult ConditionEvaluator::evaluate(const ConditionDef& cond,
                                              const PipelineState& state,
                                              pqxx::connection& conn) const {
    stats_.total_evaluations++;

    // all_checks_passed 特殊处理：直接展开 check_registry_
    if (cond.type == "all_checks_passed") {
        ConditionResult result{cond.message, true, std::nullopt, std::nullopt, {}};
        nlohmann::json check_results = nlohmann::json::array();

        if (!cond.checks || cond.checks->empty()) return result;

        std::shared_lock lock(registry_mutex_);
        for (auto& check_name : *cond.checks) {
            auto it = check_registry_.find(check_name);
            if (it == check_registry_.end()) {
                check_results.push_back({
                    {"name", check_name}, {"passed", false},
                    {"error", "unknown check: " + check_name}
                });
                result.met = false;
                continue;
            }
            auto sub_result = it->second(cond, state, conn);
            check_results.push_back({
                {"name", check_name}, {"passed", sub_result.met},
                {"current", sub_result.current}, {"target", sub_result.target}
            });
            if (!sub_result.met) result.met = false;
        }

        result.extra["checks"] = check_results;
        result.current = static_cast<int>(cond.checks->size());
        result.target = static_cast<int>(cond.checks->size());
        if (!result.met) stats_.total_failures++;
        return result;
    }

    // ... 现有 registry_ 查找逻辑
}
```

**变更清单：**
- `condition_evaluator.cpp`：在 `evaluate()` 方法顶部新增 `all_checks_passed` 分发分支；删除 `eval_all_checks_passed` 自由函数
- `condition_evaluator.hpp`：删除 `eval_all_checks_passed` 声明
- `register_all_builtins()`：移除 `registry_["all_checks_passed"]` 注册行
- `test_condition_evaluator.cpp`：`AllBuiltinConditionsRegistered` 断言数从 12 减为 11

---

## 2. `state_cache_` + `active_workflows_` 合并

**问题：** 两个独立 map 各自由不同锁保护（`state_mutex_` 保护 `state_cache_`，`active_workflows_` 无保护），存在数据竞争且读写不原子。

**方案：** 合并为单一结构体，统一锁管理。

```cpp
// pipeline_manager.hpp — 新的私有成员
struct WorldEntry {
    PipelineState state;
    std::string workflow_name;
};

mutable std::shared_mutex world_mutex_;
std::map<std::string, WorldEntry> worlds_;
```

删除：`state_cache_`、`active_workflows_`、`state_mutex_`、`debounce_mutex_`、`last_eval_time_`、`DEBOUNCE_WINDOW`。

新增 `mutable std::mutex debounce_mutex_` 和 `last_eval_time_` 保留在原位（与合并无关，独立功能）。

**影响面：** 以下方法需要适配 `worlds_` 访问模式：

| 方法 | 适配 |
|------|------|
| `get_state()` | `worlds_.find(id)->state` |
| `init_state_for_world()` | `worlds_[id] = WorldEntry{state, name}` |
| `save_state()` | `worlds_[id].state = state` |
| `get_view_data()` | `worlds_[id].state` + `.workflow_name` |
| `activate_workflow()` | `worlds_[id].workflow_name = name` |
| `advance_phase()` | `worlds_[id].state` + `.workflow_name` |
| `on_world_event()` | 同上 |
| `evaluate_phase_conditions()` | `worlds_[id].workflow_name` |
| `get_phase_context()` 等 4 个 context 方法 | `worlds_[id].workflow_name` |
| `snapshot_to_json()` / `restore_from_snapshot()` | `worlds_[id].workflow_name` |
| `handle_advance_failure()` | 通过 `save_state()`，无需修改 |
| `clear_last_error()` | `worlds_[id].state.extra.erase()` |
| `get_metrics()` | `worlds_.size()` 统计活跃数 |
| `initialize()` 恢复 | `worlds_[id].state = ...` |
| `persist_state()` / `load_state_from_db()` | 无需修改（操作参数 `state`） |

---

## 3. `init_state_for_world` 硬编码修复

**问题：** 无论 `activate_workflow()` 传什么 workflow 名，`init_state_for_world()` 都调用 `get_workflow("default_creative_pipeline")`，并在末尾覆盖 `active_workflows_`。

**方案：**

```cpp
void PipelineManager::init_state_for_world(const std::string& world_id) {
    // 从已设置的 workflow 名读取，没有则 fallback 默认
    std::string workflow_name;
    {
        std::shared_lock lock(world_mutex_);
        auto it = worlds_.find(world_id);
        workflow_name = (it != worlds_.end()) ? it->second.workflow_name
                                               : "default_creative_pipeline";
    }

    const auto* wf = get_workflow(workflow_name);
    if (!wf) {
        spdlog::warn("PipelineManager: workflow '{}' not found", workflow_name);
        return;
    }

    const auto* initial = wf->initial_phase();
    if (!initial) return;

    PipelineState state;
    state.world_id = world_id;
    state.current_phase = creative_phase_from_string(initial->key)
                              .value_or(CreativePhase::Worldbuilding);
    state.last_updated = current_iso_timestamp();
    state.active_workflow = wf->name;

    {
        std::unique_lock lock(world_mutex_);
        worlds_[world_id] = WorldEntry{state, wf->name};
    }

    persist_state(state);

    // 不需要再赋值 active_workflows_（已由 WorldEntry 持有）
    // Emit initial SSE
    if (deps_.event_emitter) {
        AdvanceRequest dummy_req{world_id, state.current_phase, "init", "system", false, false};
        emit_phase_changed(state, initial, dummy_req);
    }
}
```

---

## 4. `deps_` 空指针防护

**问题：** `condition_evaluator` 和 `pg_connection_factory` 从未 null 检查；`event_emitter` 到处都有检查，不一致。

**方案：** 构造函数校验必须依赖。

```cpp
PipelineManager::PipelineManager(Dependencies deps) : deps_(std::move(deps)) {
    if (!deps_.condition_evaluator) {
        throw std::invalid_argument("PipelineManager: condition_evaluator is required");
    }
    if (!deps_.pg_connection_factory) {
        throw std::invalid_argument("PipelineManager: pg_connection_factory is required");
    }
    start_time_ = std::chrono::steady_clock::now();
}
```

`event_emitter` 保持现有的 `if (deps_.event_emitter)` 模式——可选依赖。

---

## 5. 移除假实现检查函数

**问题：** `eval_plot_coherence` 和 `eval_pacing` 硬编码返回 `true`，`eval_foreshadow_management` 是 `eval_orphaned_foreshadowing` 的无意义代理。三者均无 workflow 引用，无 SQL 规格，本质是 LLM 级质量门禁不应作为 SQL condition 存在。

**方案：** 彻底移除。

**变更清单：**

| 文件 | 变更 |
|------|------|
| `condition_evaluator.hpp` | 删除 `eval_plot_coherence`、`eval_foreshadow_management`、`eval_pacing` 声明；删除 `CheckEvalFn` typedef（仅 check_registry_ 使用，可用 `ConditionEvalFn` 替代） |
| `condition_evaluator.cpp` | 删除三个函数体；`register_all_builtins()` 的 `check_registry_` 只保留 4 个：`character_consistency`、`diary_completeness`、`relation_currency`、`scene_completeness` |
| `test_condition_evaluator.cpp` | `AllBuiltinChecksRegistered` 断言从 `EXPECT_GE(checks.size(), 7u)` 改为 `EXPECT_EQ(checks.size(), 4u)`；删除对 `plot_coherence`、`foreshadow_management`、`pacing` 的预期 |

---

## 6. `record_transition` id 一致性

**问题：** `PhaseTransitionRecord.id` 用 `generate_uuid()` 生成，但写入 DB 时不传 id（依赖 SERIAL 自增），读回的是 `"1"`, `"2"` 而非原 UUID。

**方案：**

```sql
-- ensure_tables() 中 pipeline_history DDL 变更
CREATE TABLE IF NOT EXISTS pipeline_history (
    id              VARCHAR(64) PRIMARY KEY,  -- 改为 VARCHAR，用应用层 UUID
    world_id        VARCHAR(64) NOT NULL REFERENCES worlds(id) ON DELETE CASCADE,
    ...
);
```

```cpp
// record_transition() 中 INSERT 变更
txn.exec_params0(R"(
    INSERT INTO pipeline_history (id, world_id, from_phase, to_phase, trigger_type,
                                   triggered_by, conditions_json)
    VALUES ($1, $2, $3, $4, $5, $6, $7)
)", record.id, ...);
```

---

## 7. `kind_filter` 白名单

**问题：** `eval_entity_count` 中 `kind_filter` 仅有 `txn.quote()` 转义，无合法性验证。

**方案：**

```cpp
namespace {
const std::set<std::string> VALID_KINDS = {
    "individual", "god", "group", "creature", "force",
    "organization", "location_bound", "abstract"
};
}

// eval_entity_count() 中 entity 白名单校验之后
if (cond.kind_filter) {
    if (!VALID_KINDS.count(*cond.kind_filter)) {
        spdlog::warn("eval_entity_count: unknown kind '{}'", *cond.kind_filter);
        result.met = false;
        result.extra["error"] = "unknown kind: " + *cond.kind_filter;
        return result;
    }
}
```

---

## 8. `pqxx::work` → `pqxx::read_transaction`

**问题：** 12 个 eval 函数创建 `pqxx::work`（写事务）但只做 SELECT。浪费数据库资源，可能造成不必要的锁竞争。

**方案：** 所有 eval 函数中将 `pqxx::work txn(conn)` 替换为 `pqxx::read_transaction txn(conn)`。

影响的函数：`eval_entity_count`、`eval_all_characters_have_cards`、`eval_world_has_rule_system`、`eval_scene_count_in_chapter`、`eval_all_scenes_ended`、`eval_has_more_chapters`、`eval_scene_completeness`、`eval_diary_completeness`、`eval_relation_currency`、`eval_orphaned_foreshadowing`、`eval_character_consistency`、`eval_user_confirmed`。

`get_metrics()` 中的 `SELECT COUNT(*)` 也改为 `read_transaction`。

---

## 9. operator/field 白名单消除重复

**问题：** `evaluate_loop_condition`（pipeline_manager.cpp）和 `validate_workflow_def`（pipeline_validation.cpp）各自硬编码了相同的 operator 和 field 列表。

**方案：** 在 `pipeline_workflow_def.hpp` 中提取公共常量：

```cpp
// pipeline_workflow_def.hpp
inline const std::set<std::string> VALID_LOOP_OPS = {"<", ">", "<=", ">=", "==", "!="};

inline const std::set<std::string> VALID_LOOP_FIELDS = {
    "scene_count", "total_scenes_target",
    "chapter_count", "total_chapters_target", "cycle_count"
};
```

两处引用改为使用公共常量，删除重复定义。

---

## 10. 移除未使用的私有成员 `condition_evaluator_`

**问题：** pipeline_manager.hpp 声明了 `std::shared_ptr<ConditionEvaluator> condition_evaluator_`，但所有代码走 `deps_.condition_evaluator`，该成员从未赋值或读取。

**方案：** 删除头文件中该成员声明。

---

## 11. `ensure_tables` FK 移除

**问题：** `pipeline_states` 和 `pipeline_history` 的 `REFERENCES worlds(id)` 可能在 `worlds` 表还不存在时导致建表失败。

**方案：** 移除 FK 约束，由应用层保证引用完整性。`ON DELETE CASCADE` 改为应用层定时清理。

```sql
-- ensure_tables() 中变更
CREATE TABLE IF NOT EXISTS pipeline_states (
    ...
    world_id VARCHAR(64) NOT NULL,  -- 移除 REFERENCES worlds(id) ON DELETE CASCADE
    ...
);

CREATE TABLE IF NOT EXISTS pipeline_history (
    ...
    world_id VARCHAR(64) NOT NULL,  -- 移除 REFERENCES worlds(id) ON DELETE CASCADE
    ...
);
```

---

## 12. 迁移文件删除

**问题：** 项目未上线，迁移无意义。且 `last_error` 列与 `state.extra["last_error"]` 方案不一致。

**方案：** 删除以下文件：
- `libs/worldbuilding/sql/migrations/2026-06-09-pipeline-fix.sql`
- `libs/worldbuilding/sql/migrations/2026-06-09-pipeline-fix-rollback.sql`

`ensure_tables()` 中补充两个索引（原在迁移脚本中）：

```sql
CREATE INDEX IF NOT EXISTS idx_pipeline_history_created_at
    ON pipeline_history(created_at);

CREATE INDEX IF NOT EXISTS idx_pipeline_history_world_id
    ON pipeline_history(world_id);
```

---

## 13. 前端：WorkflowMonitor 挂载

**问题：** `WorkflowMonitor` 已实现但未在任何组件中渲染。

**方案：** 在以下两个文件中，紧接 `<PipelineNavigator />` 之后添加 `<WorkflowMonitor />`：

| 文件 | 行号 | 变更 |
|------|------|------|
| `webui/src/components/WorldSidebar.tsx` | 70 后 | 加 `<WorkflowMonitor />` |
| `webui/src/components/Sidebar.tsx` | 37 后 | 加 `<WorkflowMonitor />` |

各文件需补充 import：
```typescript
import WorkflowMonitor from './Sidebar/WorkflowMonitor';
```

---

## 14. 前端：状态字段清理

**删除：**
- `PipelineError` 接口（types.ts）—— 无消费者
- `PIPELINE_HISTORY_LOADED` action 类型及 reducer case —— 纯 no-op
- `PipelineHistoryRecord` 接口（types.ts）—— 仅被上述 no-op action 引用
- `pipelineAvailableWorkflows` 全局状态 —— 与 WorkflowMonitor 本地 useState 重复
- `PIPELINE_WORKFLOWS_LOADED`、`PIPELINE_WORKFLOW_ACTIVATED`、`PIPELINE_HISTORY_LOADED` action type
- `getPipelineHistory()` API 函数（client.ts）—— 无调用方

**修改：**
- `pipelineAutoAdvance`：SSE `pipeline_phase_changed` handler 中根据后端返回的 `auto_advance` 字段同步更新
- `showPhaseAdvancePrompt`：PipelineNavigator 中渲染确认弹窗
- `pipelineCycleComplete`：PipelineNavigator 中渲染完成提示 banner

**保留但激活：**
- `getPipelineState()`：PipelineNavigator mount 时调用，初始化 state

---

## 15. 前端：`advancePipeline()` 错误统一到 banner

**方案：** `handlePhaseClick` catch 块中：

```typescript
} catch (err) {
  dispatch({
    type: 'PIPELINE_ADVANCE_FAILED',
    reason: err instanceof Error ? err.message : 'Unknown error'
  });
  alert(`Pipeline advance failed: ${err instanceof Error ? err.message : 'Unknown error'}`);
}
```

成功时清除旧错误：
```typescript
// advancePipeline() 成功后
dispatch({ type: 'PIPELINE_ERROR_CLEARED' });
```

---

## 16. 前端：`showPhaseAdvancePrompt` 确认弹窗

**方案：** PipelineNavigator 中当 `state.showPhaseAdvancePrompt` 非 null 时渲染确认弹窗：

```tsx
{state.showPhaseAdvancePrompt && (
  <div className={styles.confirmOverlay}>
    <div className={styles.confirmDialog}>
      <p>Conditions met for phase: {state.showPhaseAdvancePrompt.next_phase}</p>
      <button onClick={() => {
        advancePipeline(state.worldId!, state.showPhaseAdvancePrompt!.next_phase, false);
        dispatch({ type: 'CLEAR_PHASE_ADVANCE_PROMPT' });
      }}>Advance</button>
      <button onClick={() => dispatch({ type: 'CLEAR_PHASE_ADVANCE_PROMPT' })}>Cancel</button>
    </div>
  </div>
)}
```

新增 action type `CLEAR_PHASE_ADVANCE_PROMPT` 及对应 reducer case。

---

## 17. 前端：`pipelineCycleComplete` 完成提示

**方案：** PipelineNavigator 中当 `state.pipelineCycleComplete` 非 null 时渲染提示 banner：

```tsx
{state.pipelineCycleComplete && (
  <div className={styles.completeBanner}>
    {state.pipelineCycleComplete}
    <button onClick={() => dispatch({ type: 'CLEAR_CYCLE_COMPLETE' })}>Dismiss</button>
  </div>
)}
```

新增 action type `CLEAR_CYCLE_COMPLETE` 及对应 reducer case。

---

## 18. 前端：`getPipelineState()` 初始加载

**方案：** PipelineNavigator 新增 useEffect：

```typescript
useEffect(() => {
  if (!state.worldId) return;
  getPipelineState(state.worldId)
    .then(view => dispatch({ type: 'SET_PIPELINE_VIEW', ...view }))
    .catch(console.error);
}, [state.worldId]);
```

已有 `SET_PIPELINE_VIEW` action type。

---

## 19. 前端：测试修复

**PipelineNavigator.test.tsx：**
- 补全 `useEffect` deps 数组
- `toBeDefined()` → `toBeInTheDocument()`

**WorkflowMonitor.test.tsx：**
- 补全 `useEffect` deps 数组
- 新增测试：`activatePipelineWorkflow` 失败时恢复选项
- 新增测试：只有 1 个 workflow 时无选择器

---

## 20. 前端：`activatePipelineWorkflow` 失败恢复

```typescript
const handleWorkflowChange = async (name: string) => {
  const prev = selectedWorkflow;
  setSelectedWorkflow(name);
  try {
    await activatePipelineWorkflow(state.worldId!, name);
  } catch (err) {
    setSelectedWorkflow(prev);  // 恢复之前的值
    alert(`Failed to activate workflow: ${err instanceof Error ? err.message : 'Unknown error'}`);
  }
};
```

---

## 文件变更清单

| 文件 | 动作 | 涉及段 |
|------|------|--------|
| `libs/worldbuilding/include/merak/worldbuilding/condition_evaluator.hpp` | MOD | 1, 5 |
| `libs/worldbuilding/src/condition_evaluator.cpp` | MOD | 1, 5, 7, 8 |
| `libs/worldbuilding/include/merak/worldbuilding/pipeline_manager.hpp` | MOD | 2, 10 |
| `libs/worldbuilding/src/pipeline_manager.cpp` | MOD | 2, 3, 4, 6, 8, 11, 12 |
| `libs/worldbuilding/include/merak/worldbuilding/pipeline_workflow_def.hpp` | MOD | 9 |
| `libs/worldbuilding/src/pipeline_validation.cpp` | MOD | 9 |
| `libs/worldbuilding/tests/test_condition_evaluator.cpp` | MOD | 1, 5 |
| `libs/worldbuilding/tests/test_pipeline_manager.cpp` | MOD | 2, 3 |
| `libs/worldbuilding/tests/test_pipeline_validation.cpp` | MOD | 9 |
| `libs/worldbuilding/sql/migrations/2026-06-09-pipeline-fix.sql` | DEL | 12 |
| `libs/worldbuilding/sql/migrations/2026-06-09-pipeline-fix-rollback.sql` | DEL | 12 |
| `webui/src/api/types.ts` | MOD | 14 |
| `webui/src/api/client.ts` | MOD | 14 |
| `webui/src/AppState.tsx` | MOD | 14, 16, 17 |
| `webui/src/components/WorldSidebar.tsx` | MOD | 13 |
| `webui/src/components/Sidebar.tsx` | MOD | 13 |
| `webui/src/components/Sidebar/PipelineNavigator.tsx` | MOD | 15, 16, 17, 18 |
| `webui/src/components/Sidebar/PipelineNavigator.module.css` | MOD | 16, 17 |
| `webui/src/components/Sidebar/WorkflowMonitor.tsx` | MOD | 20 |
| `webui/src/__tests__/PipelineNavigator.test.tsx` | MOD | 19 |
| `webui/src/__tests__/WorkflowMonitor.test.tsx` | MOD | 19 |
