# PipelineManager 修复设计

> 对 2026-06-09 pipeline-manager-implementation 的 Review 发现 11 个问题进行完整修复。

## 1. SSE 广播：扩展 RuntimeService 事件模型

**问题：** PipelineManager 所有 SSE 事件携带空 session_id，`emit_event()` 校验 session 存在性时抛异常。

**方案：** 新增 world 级广播语义——管线事件属于某个 world，该 world 下所有在线 session 都应收到。

### 1.1 RuntimeService 变更

```cpp
// runtime_service.hpp 新增
public:
    void broadcast_to_world(const std::string& world_id, const RuntimeEvent& event);

private:
    // world_id → 关联的 session_id 集合
    std::map<std::string, std::set<std::string>> world_sessions_;
```

`create_session()` 接受 `world_id` 参数已有，在创建 session 时将映射写入 `world_sessions_`。

```cpp
// runtime_service.cpp
void RuntimeService::broadcast_to_world(const std::string& world_id, const RuntimeEvent& event) {
    std::lock_guard lock(mutex_);
    auto it = world_sessions_.find(world_id);
    if (it == world_sessions_.end()) return;
    for (auto& sid : it->second) {
        bus_.publish(event); // EventBus 按 session 路由，同样复制给每个 session
    }
}
```

### 1.2 main.cpp 变更

```cpp
.event_emitter = [&runtime](const merak::RuntimeEvent& e) {
    auto wid = e.payload.value("world_id", "");
    if (!wid.empty()) runtime->broadcast_to_world(wid, e);
},
```

### 1.3 PipelineManager 端

所有 `RuntimeEvent` 构造处确认 payload 含 `world_id`（目前已含，无需改动）。

---

## 2. ConditionEvaluator 重构

### 2.1 移除隐患

- 移除 `eval_custom_sql`——可执行任意 SQL，无合理使用场景
- 移除 `ConditionOp::CONTAINS`、`EXISTS`——有定义无实现
- 移除 `op_from_string` 中对应分支

### 2.2 entity_count 白名单

```cpp
namespace {
const std::map<std::string, std::string> ENTITY_TABLES = {
    {"agents", "agents"},
    {"locations", "locations"},
    {"chapters", "chapters"},
    {"scenes", "scenes"},
    {"agent_relations", "agent_relations"},
    {"foreshadowings", "foreshadowings"},
    {"agent_diaries", "agent_diaries"},
    {"world_knowledge", "world_knowledge"},
    {"character_cards", "character_cards"},
};
} // namespace

ConditionResult eval_entity_count(...) {
    auto table_it = ENTITY_TABLES.find(cond.entity);
    if (table_it == ENTITY_TABLES.end()) {
        spdlog::warn("eval_entity_count: unknown entity '{}'", cond.entity);
        result.met = false;
        return result;
    }
    std::string query = "SELECT COUNT(*) FROM " + table_it->second + " WHERE world_id = $1";
    // ...
}
```

### 2.3 eval_all_checks_passed 改写

不再返回硬编码 true，改为遍历 `checks` 列表，每个子检查调用对应评估函数：

```cpp
ConditionResult eval_all_checks_passed(const ConditionDef& cond,
                                        const PipelineState& state,
                                        pqxx::connection& conn) {
    ConditionResult result{cond.message, true, std::nullopt, std::nullopt, {}};
    if (!cond.checks) return result;

    nlohmann::json check_results = nlohmann::json::array();
    for (auto& check_name : *cond.checks) {
        ConditionDef sub_cond;
        sub_cond.type = check_name;
        sub_cond.message = check_name;
        auto sub_result = ConditionEvaluator::instance().evaluate(sub_cond, state, conn);
        check_results.push_back({
            {"name", check_name},
            {"passed", sub_result.met},
            {"current", sub_result.current},
            {"target", sub_result.target}
        });
        if (!sub_result.met) result.met = false;
    }
    result.extra["checks"] = check_results;
    result.current = static_cast<int>(cond.checks->size());
    result.target = static_cast<int>(cond.checks->size());
    return result;
}
```

### 2.4 新增 4 个子检查内置条件

| 条件类型 | SQL / 逻辑 | 说明 |
|----------|-----------|------|
| `diary_completeness` | 当前章节所有 scene 的参与角色均有 `agent_diaries` 条目 | Reflection 后进入下一章，缺失日记导致 agent 缺上下文 |
| `relation_currency` | 当前章节内 `agent_relations` 的 `updated_at` 不早于本章第一个 scene 的 `created_at` | 确保关系反映了本章事件 |
| `orphaned_foreshadowing` | `SELECT COUNT(*) FROM foreshadowings WHERE world_id=$1 AND status='open' AND updated_at=created_at` | 只抓从未被触碰的遗忘伏笔，长线伏笔不受影响 |
| `scene_completeness` | 当前章节无 `status='draft'` 的 scene，所有 scene 有非空 `narrative` | 确保无半成品场景 |

### 2.5 character_consistency（diary_completeness 的增强版）

在 `diary_completeness` 基础上增加 Secret 泄露检测——检查角色日记中是否出现其不应知晓的 Secret 关键词：

```cpp
ConditionResult eval_character_consistency(const ConditionDef& cond,
                                            const PipelineState& state,
                                            pqxx::connection& conn) {
    // Step 1: 检查日记完整性
    auto diary_result = eval_diary_completeness(cond, state, conn);
    if (!diary_result.met) return diary_result;

    // Step 2: Secret 泄露检测——对每个活跃角色
    pqxx::work txn(conn);
    // 获取所有活跃 Secret
    auto secrets = txn.exec_params(
        "SELECT id, truth, public_version FROM secrets "
        "WHERE world_id = $1 AND status = 'active'", state.world_id);

    // 对每个角色，找出其不应知晓的 Secret
    for (auto& secret_row : secrets) {
        auto sid = secret_row["id"].as<std::string>();
        auto truth = secret_row["truth"].as<std::string>();

        // 找出不知道此 Secret 的角色
        auto unaware = txn.exec_params(
            "SELECT a.id, a.name FROM agents a "
            "WHERE a.world_id = $1 AND a.kind = 'individual' "
            "AND NOT EXISTS (SELECT 1 FROM secrets s2 "
            "  WHERE s2.id = $2 AND a.id = ANY(s2.aware_character_ids))",
            state.world_id, sid);

        for (auto& agent_row : unaware) {
            auto aid = agent_row["id"].as<std::string>();
            // 检查该角色近期日记是否包含 Secret truth 的关键词
            auto diary_rows = txn.exec_params(
                "SELECT content FROM agent_diaries WHERE agent_id = $1 "
                "ORDER BY created_at DESC LIMIT 5", aid);
            for (auto& d : diary_rows) {
                auto content = d["content"].as<std::string>();
                // 关键词匹配：truth 中长度 >= 4 的词或短语
                // 简单分词，在 content 中搜索
            }
        }
    }
    return diary_result; // 暂不阻断，仅记录到 extra
}
```

**注意：** 关键词匹配容易误报。初期只在 `extra` 中报告可疑线索，不阻断阶段推进。待后续 LLM 集成后再增强。

### 2.6 更新 default_creative_pipeline.json 的 reflection 阶段

```json
"advance_when": {
  "operator": "and",
  "conditions": [
    { "type": "diary_completeness", "message": "所有角色日记完整" },
    { "type": "relation_currency", "message": "角色关系已反映本章变化" },
    { "type": "orphaned_foreshadowing", "message": "不存在被遗忘的伏笔" },
    { "type": "scene_completeness", "message": "无半成品场景" }
  ]
}
```

---

## 3. AutoLoopDef 激活

**问题：** `AutoLoopDef` 解析完整但 `PipelineManager` 未读取执行。

**方案：** 无需在 advance_phase 中新增循环控制。路径是：

1. `advance_phase` → 进入 `scene_writing` 时，若目标阶段有 `auto_loop`，正常执行推进
2. `on_world_event` 中：当场景创建事件触发条件求值时，若当前阶段的 `auto_loop.continue_while` 表达式仍满足，不触发自动推进；仅当表达式不满足 + 所有条件满足时才推进
3. `continue_while` 表达式解析：简单格式为 `"field op value"`，如 `"scene_count < total_scenes_target"`，映射到 PipelineState 的对应字段

```cpp
// pipeline_manager.cpp: on_world_event 末尾
if (summary.all_met) {
    // 检查 auto_loop：如果继续条件仍满足，不推进
    const auto* current_def = wf->get_phase(state_opt->current_phase);
    if (current_def && current_def->auto_loop) {
        bool should_continue = evaluate_loop_condition(
            *current_def->auto_loop, *state_opt);
        if (should_continue) return; // 停留在当前阶段继续循环
    }
    // 正常推进...
}

bool PipelineManager::evaluate_loop_condition(
    const AutoLoopDef& loop, const PipelineState& state) {
    // 解析 continue_while: "scene_count < total_scenes_target"
    // 简单实现：比较 state 的对应字段
    auto parts = split(loop.continue_while, ' ');
    if (parts.size() == 3) {
        int current = get_state_field(state, parts[0]);   // scene_count_in_chapter
        int target = get_state_field(state, parts[2]);     // total_scenes_target
        return compare(current, parts[1], target);          // <
    }
    return false;
}
```

---

## 4. Config 部署路径修复

**问题：** `pipeline_config_dir` 设为 `~/.merak/pipelines`，配置文件在源码树，无安装步骤。

**方案：**

### 4.1 CMakeLists.txt 增加安装指令

```cmake
install(DIRECTORY config/pipelines DESTINATION share/merak)
```

### 4.2 main.cpp 路径改为 exe 相对路径

参考 webui 的解析模式：

```cpp
auto exe = exe_dir_path();
auto pipeline_path = exe / "pipelines";
// 如果 exe 路径下不存在，fallback 到源码树路径
if (!std::filesystem::exists(pipeline_path)) {
    pipeline_path = exe / ".." / "config" / "pipelines";
}
// ...
.pipeline_config_dir = pipeline_path,
```

---

## 5. 前端工作流选择器

**问题：** `listPipelineWorkflows` / `activatePipelineWorkflow` 已实现但无 UI 调用。

**方案：** 在 `WorkflowMonitor` 顶部增加下拉框。

```tsx
// WorkflowMonitor.tsx 新增
import { listPipelineWorkflows, activatePipelineWorkflow } from '../../api/client';

// 组件内
const [workflows, setWorkflows] = useState<WorkflowSummary[]>([]);
const [selectedWorkflow, setSelectedWorkflow] = useState(state.pipelineActiveWorkflow);

useEffect(() => {
  listPipelineWorkflows().then(setWorkflows).catch(console.error);
}, []);

const handleWorkflowChange = async (name: string) => {
  setSelectedWorkflow(name);
  await activatePipelineWorkflow(state.worldId!, name);
};

// JSX: 在 title 下方
{workflows.length > 1 && (
  <select value={selectedWorkflow} onChange={e => handleWorkflowChange(e.target.value)}>
    {workflows.map(w => <option key={w.name} value={w.name}>{w.description}</option>)}
  </select>
)}
```

---

## 6. const_correctness

**问题：** `get_view_data`（const）用 `const_cast` 绕过 `evaluate_phase_conditions` 和 `load_recent_history` 的非 const 声明。

**方案：** 将两个方法标记为 `const`，移除 `const_cast`。

这两个方法实际上只读操作（读 active_workflows_、调用 pg_connection_factory、执行 SQL 查询），不修改任何成员。

---

## 7. auto-advance 失败事件

**问题：** 自动推进失败时前端无感知。

**方案：** 在 `on_world_event` 中，`advance_phase` 返回非 SUCCESS 时发射事件：

```cpp
auto result = advance_phase(auto_req);
if (result != AdvanceResult::SUCCESS && deps_.event_emitter) {
    RuntimeEvent fail_event;
    fail_event.type = "pipeline_advance_failed";
    fail_event.payload = {
        {"world_id", world_id},
        {"phase", to_string(state_opt->current_phase)},
        {"reason", advance_result_to_string(result)}
    };
    deps_.event_emitter(fail_event);
}
```

前端 AppState 新增 SSE handler：
```typescript
case 'pipeline_advance_failed':
  return { ...state, pipelineAdvanceError: p.reason as string };
```

---

## 8. advancePipeline 失败反馈

**问题：** 前端错误只 `console.error`，用户无感知。

**方案：** 在 PipelineNavigator 的 `handlePhaseClick` 中，失败时 `alert()` 显示服务端返回的消息：

```tsx
} catch (err) {
  alert(`管线推进失败: ${err instanceof Error ? err.message : '未知错误'}`);
}
```

---

## 9. ConditionEvaluator 注入化

**问题：** 单例模式导致测试无法 mock，也无法支持不同场景使用不同条件集。

**方案：** 保留单例作为默认获取方式，但 `PipelineManager` 内部存储 `std::shared_ptr<ConditionEvaluator>`，通过构造函数注入，默认使用 `ConditionEvaluator::instance()` 的共享指针包装。

```cpp
// pipeline_manager.hpp
class PipelineManager {
    // ...
    std::shared_ptr<ConditionEvaluator> condition_evaluator_;
};

// pipeline_manager.cpp - 初始化时
condition_evaluator_ = std::shared_ptr<ConditionEvaluator>(
    &ConditionEvaluator::instance(), [](auto*){}); // non-owning
```

---

## 文件变更清单

| 文件 | 动作 | 说明 |
|------|------|------|
| `libs/runtime/include/merak/runtime_service.hpp` | MOD | 新增 broadcast_to_world + world_sessions_ |
| `libs/runtime/src/runtime_service.cpp` | MOD | 实现 broadcast_to_world; create_session 写入 world_sessions_ |
| `cli/src/main.cpp` | MOD | event_emitter 改用 broadcast_to_world; config 路径修复 |
| `libs/worldbuilding/src/condition_evaluator.cpp` | MOD | 移除 custom_sql/CONTAINS/EXISTS; entity_count 白名单; 重写 all_checks_passed; 新增 5 个子检查 |
| `libs/worldbuilding/include/merak/worldbuilding/condition_evaluator.hpp` | MOD | 新增 eval_diary_completeness 等声明; 移除 eval_custom_sql |
| `libs/worldbuilding/include/merak/worldbuilding/pipeline_workflow_def.hpp` | MOD | 移除 ConditionOp::CONTAINS/EXISTS |
| `libs/worldbuilding/src/pipeline_workflow_def.cpp` | MOD | 移除 op_from_string 中对应分支 |
| `libs/worldbuilding/src/pipeline_manager.cpp` | MOD | AutoLoopDef 激活; auto-advance 失败事件; const_correctness; ConditionEvaluator 注入 |
| `libs/worldbuilding/include/merak/worldbuilding/pipeline_manager.hpp` | MOD | evaluate_phase_conditions/load_recent_history 标记 const; ConditionEvaluator 注入 |
| `config/pipelines/default_creative_pipeline.json` | MOD | reflection 阶段 conditions 改为新 4 项 |
| `CMakeLists.txt` | MOD | install(DIRECTORY config/pipelines ...) |
| `webui/src/components/Sidebar/WorkflowMonitor.tsx` | MOD | 工作流选择器下拉框 |
| `webui/src/components/Sidebar/PipelineNavigator.tsx` | MOD | 失败时 alert 显示错误 |
| `webui/src/AppState.tsx` | MOD | pipeline_advance_failed SSE handler |
