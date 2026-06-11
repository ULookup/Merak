# PipelineManager — 创作管线引擎完整设计

> Issue #71: 创作管线 PipelineState 仅定义未接入 → 前端管线导航器为静态死展示

## 1. 问题分析

### 1.1 已存在的资产（价值充足但完全闲置）

| 资产 | 位置 | 状态 |
|------|------|------|
| `CreativePhase` 枚举（5阶段） | `pipeline.hpp:9` | 完成 |
| `PipelineState` 结构体 | `pipeline.hpp:42` | 完成 |
| `allowed_next_phases()` 状态机 | `pipeline.cpp:5` | 完成（含双向可逆边） |
| `generate_phase_context()` 5套阶段提示词 | `pipeline.cpp:21` | 完成 |
| `pipeline_snapshot_json` checkpoint 字段 | `checkpoint.hpp:25` | 声明但无人填充 |
| 前端 `PipelineNavigator` 5阶段 UI | `PipelineNavigator.tsx` | 完成 |
| 前端 `pipeline_phase_changed` SSE handler | `AppState.tsx:730` | 完成 |
| 前端 `CreativePhase` + `PipelineState` 类型 | `api/types.ts:443` | 完成 |

### 1.2 四个断裂点

1. **SSE 事件不发射** — 全库无一处调用 `emit_event("pipeline_phase_changed")`。前端 `AppState.tsx:730` 的 handler 永不触发。`state.pipelinePhase` 永远为 `null` → fallback `'worldbuilding'`。PipelineNavigator 永久冻结在第1阶段。

2. **Agent Loop 不注入阶段上下文** — `generate_phase_context()` 有5套详细提示词但从未进入 system prompt。`RuntimeService::build_prompt_profile()` 完全不引用 pipeline。

3. **HTTP 不暴露** — 没有 REST 端点读取/更新当前 phase。前端无法主动查询或切换阶段。

4. **PipelineState 无人读写** — 没有任何代码创建或维护活跃的 `PipelineState` 实例。checkpoint 声明了 `pipeline_snapshot_json` 但从无人填充。阶段状态无持久化，崩溃即丢失。

## 2. 设计概览：文件清单

共 **19 个文件**（9 新增，10 修改）。

### 2.1 新增文件（C++ — libs/worldbuilding/）

| 文件 | 内容 |
|------|------|
| `libs/worldbuilding/include/merak/worldbuilding/pipeline_manager.hpp` | PipelineManager 类声明 |
| `libs/worldbuilding/src/pipeline_manager.cpp` | PipelineManager 完整实现 |
| `libs/worldbuilding/include/merak/worldbuilding/pipeline_workflow_def.hpp` | 工作流定义、Phase 定义、条件定义的数据结构 |
| `libs/worldbuilding/src/pipeline_workflow_def.cpp` | 工作流定义的 JSON 解析器 |
| `libs/worldbuilding/include/merak/worldbuilding/condition_evaluator.hpp` | 条件求值器接口 + 内置条件声明 |
| `libs/worldbuilding/src/condition_evaluator.cpp` | 条件求值器实现（9+ 内置条件） |

### 2.2 新增文件（其他）

| 文件 | 内容 |
|------|------|
| `libs/worldbuilding/sql/pipeline_schema.sql` | pipeline_states + pipeline_history DDL |
| `config/pipelines/default_creative_pipeline.json` | 默认5阶段创作管线定义 |
| `webui/src/components/Sidebar/WorkflowMonitor.tsx` | 工作流运行状态面板 |

### 2.3 修改文件

| 文件 | 改动内容 |
|------|----------|
| `libs/worldbuilding/include/merak/worldbuilding/pipeline.hpp` | PipelineState 增加字段 |
| `libs/runtime/include/merak/runtime_service.hpp` | 增加 PipelineManager 持有 + 接口 |
| `libs/runtime/src/runtime_service.cpp` | 初始化 PipelineManager、事件桥接、context 注入 |
| `libs/prompts/include/merak/prompts/types.hpp` | PromptProfile 增加 phase_context / phase_allowed_tools 字段 |
| `libs/http/src/worldbuilding_http_handler.cpp` | 新增 4 个 pipeline REST 端点 |
| `cli/src/main.cpp` | 初始化时创建 PipelineManager 并注入依赖 |
| `webui/src/components/Sidebar/PipelineNavigator.tsx` | 条件进度条、点击交互、hover tooltip |
| `webui/src/AppState.tsx` | 完善 pipeline 相关 SSE 事件处理 |
| `webui/src/api/client.ts` | 新增 pipeline API 调用函数 |
| `webui/src/api/types.ts` | 新增 ConditionState 等前端类型 |

## 3. 核心数据结构 — pipeline_workflow_def.hpp

### 3.1 ConditionOp 枚举

```cpp
enum class ConditionOp { EQ, NEQ, GT, GTE, LT, LTE, CONTAINS, EXISTS };
```

配 `op_from_string()` 将 JSON 中的字符串（ "=="、"gte" 等）转换为枚举。

### 3.2 ConditionDef — 单个条件定义

```cpp
struct ConditionDef {
    std::string type;                         // "entity_count" | "all_characters_have_cards" | ...
    std::string entity;                        // 目标实体：agent / location / chapter / scene / ...
    std::optional<std::string> kind_filter;    // 可选类型过滤：individual / god / ...
    ConditionOp op = ConditionOp::GTE;
    std::optional<int> target_int;            // 整数目标值
    std::optional<std::string> target_str;    // 字符串目标值 / 变量引用（$total_scenes_target）
    std::optional<std::vector<std::string>> checks; // all_checks_passed 的检查项列表
    std::string message;                       // 用户可见的描述
};
```

### 3.3 ConditionGroup — 条件组合

```cpp
struct ConditionGroup {
    std::string operator_type = "and"; // "and" | "or"
    std::vector<ConditionDef> conditions;
};
```

### 3.4 ActionDef — 动作定义

```cpp
struct ActionDef {
    std::string type; // "emit_sse" | "update_checkpoint" | "log" | "validate" | "goto_phase" | "conditional"
    nlohmann::json params;
};
```

### 3.5 PhaseContextConfig — Context 注入配置

```cpp
struct PhaseContextConfig {
    std::vector<std::string> inject; // ["phase_guidance","available_tools","world_summary",...]
    nlohmann::json extra;            // 额外参数，如 include_existing_characters: true
};
```

### 3.6 PhaseDefinition — 单阶段定义

```cpp
struct PhaseDefinition {
    std::string key;                             // "worldbuilding"
    std::string label;                           // "世界观构建"
    bool initial = false;                        // 是否为起始阶段

    PhaseContextConfig context;                  // 上下文注入配置
    std::vector<std::string> allowed_tools;      // 该阶段可用工具白名单
    std::optional<ConditionGroup> advance_when;  // 推进条件（空=只能手动）
    std::vector<std::string> allowed_retreat;    // 允许退回的阶段列表

    std::vector<ActionDef> on_enter;             // 进入阶段时执行的动作
    std::vector<ActionDef> on_exit;              // 退出阶段时执行的动作
    std::vector<ActionDef> on_complete;          // 阶段完成后的动作（可用于 goto_phase）

    struct AutoLoop {                            // per-scene 循环（仅 scene_writing 使用）
        std::string entity;                      // "chapter"
        std::string target;                      // "all_scenes_in_chapter"
        std::string continue_while;              // "scene_count < total_scenes_target"
    };
    std::optional<AutoLoop> auto_loop;
};
```

### 3.7 PipelineWorkflowDef — 工作流定义

```cpp
struct PipelineWorkflowDef {
    std::string name;                    // "default_creative_pipeline"
    std::string description;
    int version = 1;
    bool auto_advance = true;            // 条件满足时自动推进
    bool require_confirmation = false;   // 需要用户确认（覆盖 auto_advance）
    std::vector<PhaseDefinition> phases;

    // 辅助方法
    const PhaseDefinition* initial_phase() const;
    const PhaseDefinition* get_phase(const std::string& key) const;
    const PhaseDefinition* get_phase(CreativePhase phase) const;
};
```

### 3.8 结果结构体

```cpp
struct ConditionResult {
    std::string message;              // 条件的用户可见描述
    bool met = false;
    std::optional<int> current;       // 当前值
    std::optional<int> target;        // 目标值
    nlohmann::json extra;             // 额外信息（如检查项详情）
};

struct ConditionEvalSummary {
    std::string phase_key;
    bool all_met = false;
    std::vector<ConditionResult> results;
};
```

所有结构体均配 `from_json(const nlohmann::json&, T&)` 反序列化函数。

## 4. PipelineManager 类 — pipeline_manager.hpp

### 4.1 依赖注入

```cpp
struct Dependencies {
    std::function<std::shared_ptr<pqxx::connection>()> pg_connection_factory;
    std::function<void(const RuntimeEvent&)> event_emitter;
    std::filesystem::path pipeline_config_dir; // config/pipelines/
};
```

### 4.2 PhaseTransitionRecord

```cpp
struct PhaseTransitionRecord {
    std::string id;
    std::string world_id;
    CreativePhase from_phase;
    CreativePhase to_phase;
    std::string trigger;                    // "auto" | "manual" | "workflow_action"
    std::optional<std::string> triggered_by; // "user_click" | agent_id | event_type
    ConditionEvalSummary conditions_at_transition;
    std::string timestamp;
};
```

### 4.3 公开接口

#### 初始化

```cpp
explicit PipelineManager(Dependencies deps);
~PipelineManager();
void initialize(); // 加载工作流定义、建表、恢复活跃状态
```

#### 工作流定义管理

```cpp
void load_workflow_defs();                                           // 从 config/pipelines/*.json 加载
const PipelineWorkflowDef* get_workflow(const std::string& name) const;
std::vector<std::string> list_workflows() const;
void activate_workflow(const std::string& world_id, const std::string& workflow_name);
```

#### PipelineState CRUD

```cpp
std::optional<PipelineState> get_state(const std::string& world_id) const;
void init_state_for_world(const std::string& world_id);
void save_state(const PipelineState& state);
```

#### 阶段推进

```cpp
enum class AdvanceResult {
    SUCCESS,              // 推进成功
    INVALID_TRANSITION,   // 不允许的转换
    CONDITIONS_NOT_MET,   // 条件不满足（手动强制推进时忽略）
    ALREADY_AT_PHASE,     // 已在目标阶段
    NO_ACTIVE_STATE       // 无活跃状态
};

struct AdvanceRequest {
    std::string world_id;
    std::optional<CreativePhase> target_phase; // 空 = 自动找下一个
    std::string trigger = "manual";
    std::optional<std::string> triggered_by;
    bool force = false;     // true = 跳过条件检查（手动强制推进）
    bool skip_event = false; // true = 不发射 SSE（恢复时使用）
};

AdvanceResult advance_phase(const AdvanceRequest& req);
```

#### 条件求值

```cpp
ConditionEvalSummary evaluate_phase_conditions(const PipelineState& state) const;
ConditionResult evaluate_single_condition(const ConditionDef& cond,
                                          const PipelineState& state) const;
```

#### 事件监听（被 RuntimeService 调用）

```cpp
void on_world_event(const std::string& world_id,
                    const std::string& event_type,
                    const nlohmann::json& payload);
```

监听的事件白名单（14种）：`agent_created`, `agent_card_updated`, `relation_updated`, `scene_created`, `scene_ended`, `chapter_created`, `diary_written`, `foreshadow_planted`, `foreshadow_updated`, `secret_created`, `location_created`, `knowledge_added`, `world_time_advanced`, `memory_summary_created`

#### Context 注入

```cpp
std::string get_phase_context(const std::string& world_id) const;
std::vector<std::string> get_allowed_tools(const std::string& world_id) const;
nlohmann::json get_phase_injection_config(const std::string& world_id) const;
```

#### 前端数据

```cpp
struct PipelineViewData {
    PipelineState state;
    std::string active_workflow_name;
    ConditionEvalSummary current_conditions;
    std::vector<PhaseTransitionRecord> recent_history; // 最近10条
};
PipelineViewData get_view_data(const std::string& world_id) const;
```

#### 序列化到 Checkpoint

```cpp
std::string snapshot_to_json(const std::string& world_id) const;
void restore_from_snapshot(const std::string& world_id, const std::string& json);
```

### 4.4 私有成员

```cpp
Dependencies deps_;

// 加载的工作流定义 name → def
std::map<std::string, PipelineWorkflowDef> workflow_defs_;

// world_id → 活跃的工作流名称
std::map<std::string, std::string> active_workflows_;

// 内存缓存：world_id → PipelineState（避免频繁 PG 查询）
mutable std::shared_mutex state_mutex_;
std::map<std::string, PipelineState> state_cache_;

// 防抖：事件可能在短时间内重复触发，避免条件反复求值
mutable std::mutex debounce_mutex_;
std::map<std::string, std::chrono::steady_clock::time_point> last_eval_time_;
static constexpr auto DEBOUNCE_WINDOW = std::chrono::milliseconds(2000);

// 内部方法
void ensure_tables();
void load_state_from_db(const std::string& world_id);
void persist_state(const PipelineState& state);
void record_transition(const PhaseTransitionRecord& record);
void execute_actions(const std::vector<ActionDef>& actions, const PipelineState& state);
void emit_phase_changed(const PipelineState& state,
                        const PhaseDefinition* phase_def,
                        const AdvanceRequest& req);
bool is_transition_allowed(CreativePhase from, CreativePhase to,
                           const PhaseDefinition* phase_def) const;
```

## 5. ConditionEvaluator — 条件求值器

### 5.1 接口

```cpp
using ConditionEvalFn = std::function<ConditionResult(
    const ConditionDef& cond,
    const PipelineState& state,
    pqxx::connection& conn
)>;

class ConditionEvaluator {
public:
    static ConditionEvaluator& instance();

    void register_condition(const std::string& type, ConditionEvalFn fn);

    ConditionResult evaluate(const ConditionDef& cond,
                             const PipelineState& state,
                             pqxx::connection& conn) const;

    ConditionEvalSummary evaluate_group(const ConditionGroup& group,
                                        const PipelineState& state,
                                        pqxx::connection& conn,
                                        const std::string& phase_key) const;

private:
    ConditionEvaluator() = default;
    void register_builtins();
    std::map<std::string, ConditionEvalFn> registry_;
};
```

### 5.2 9个内置条件函数

| 条件类型 | 函数 | SQL/逻辑 |
|----------|------|----------|
| `entity_count` | `eval_entity_count` | `SELECT COUNT(*) FROM {entity} WHERE world_id=$1 [AND kind=$2]` |
| `all_characters_have_cards` | `eval_all_characters_have_cards` | `agents` LEFT JOIN `character_cards`，检查是否所有 individual 类型 agent 都有 card |
| `world_has_rule_system` | `eval_world_has_rule_system` | 检查 `world_knowledge` 中 `category='rules'` 的条目数 > 0 |
| `scene_count_in_chapter` | `eval_scene_count_in_chapter` | 检查 `active_chapter_id` 的 scenes count >= `$total_scenes_target`（支持变量引用） |
| `all_scenes_ended` | `eval_all_scenes_ended` | 检查 `active_chapter_id` 下所有 scenes `status='completed'` 且 has diary |
| `all_checks_passed` | `eval_all_checks_passed` | 调用 VoiceAnalyzer/各 Store 的检查方法，支持 `checks` 列表 |
| `has_more_chapters` | `eval_has_more_chapters` | 检查 arc 下是否有 `status='draft'` 或 `'planned'` 的 chapters |
| `user_confirmed` | `eval_user_confirmed` | 检查 `pipeline_states` 表中的 `user_confirmation` 标记 |
| `custom_sql` | `eval_custom_sql` | 直接执行 `target_str` 中的 SQL，第一列第一行与 `target_int` 比较 |

## 6. 核心实现逻辑 — pipeline_manager.cpp

### 6.1 initialize()

```
initialize()
  ├── ensure_tables()           → CREATE TABLE IF NOT EXISTS pipeline_states + pipeline_history
  ├── load_workflow_defs()      → 扫描 config/pipelines/*.json，解析为 PipelineWorkflowDef
  └── 恢复所有活跃状态           → SELECT world_id, state_json FROM pipeline_states → state_cache_
```

### 6.2 init_state_for_world(world_id)

```
init_state_for_world(world_id)
  ├── 获取 default_creative_pipeline 的 initial_phase
  ├── 创建 PipelineState{world_id, current_phase=initial, last_updated=now}
  ├── 写入 state_cache_
  ├── persist_state()           → INSERT INTO pipeline_states
  ├── 记录 active_workflows_
  └── emit pipeline_phase_changed SSE（含初始条件进度）
```

### 6.3 advance_phase(req)

```
advance_phase(req)
  ├── get_state(world_id) → 检查活跃状态
  ├── 确定目标阶段：
  │   ├── 指定了 target_phase → 直接使用
  │   └── 未指定 → allowed_next_phases(current)[0]
  ├── 校验：
  │   ├── target == current → ALREADY_AT_PHASE
  │   ├── 转换不允许 → INVALID_TRANSITION
  │   └── 向前推进 && !force && current 有 advance_when：
  │       └── evaluate_phase_conditions() → all_met? or CONDITIONS_NOT_MET
  ├── 执行 on_exit 动作（当前阶段）
  ├── 更新状态：current_phase = target, last_updated = now
  │   └── 如果 target 是 Worldbuilding 或 CharacterCreation，清除 active_scene_id
  ├── persist_state()           → UPDATE pipeline_states SET state_json=...
  ├── record_transition()       → INSERT INTO pipeline_history
  ├── 执行 on_enter 动作（目标阶段）
  └── emit_phase_changed() SSE（含条件列表、allowed_tools、next_allowed、allowed_retreat）
```

### 6.4 on_world_event(world_id, event_type, payload)

```
on_world_event(world_id, event_type, payload)
  ├── 1. 过滤：跳过不在白名单中的事件类型
  ├── 2. 防抖：同一 world 2秒内不重复求值
  ├── 3. 获取当前状态 + 工作流定义
  │   └── 非 auto_advance 模式 → 直接返回
  ├── 4. evaluate_phase_conditions()
  ├── 5. 发射 pipeline_condition_progress 事件（即使条件不满足也发，供前端更新进度条）
  └── 6. 如果 all_met：
      ├── require_confirmation 模式 → 发射 pipeline_condition_met（前端弹出确认提示）
      └── 自动模式 → advance_phase(auto_req)
```

## 7. 数据库 DDL — pipeline_schema.sql

```sql
CREATE TABLE IF NOT EXISTS pipeline_states (
    id              SERIAL PRIMARY KEY,
    world_id         VARCHAR(64) NOT NULL UNIQUE REFERENCES worlds(id) ON DELETE CASCADE,
    active_workflow  VARCHAR(128) NOT NULL DEFAULT 'default_creative_pipeline',
    state_json       JSONB NOT NULL,      -- 完整的 PipelineState 序列化
    auto_advance     BOOLEAN NOT NULL DEFAULT true,
    require_confirm  BOOLEAN NOT NULL DEFAULT false,
    user_confirm     BOOLEAN NOT NULL DEFAULT false, -- user_confirmed 条件用
    created_at       TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at       TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS pipeline_history (
    id              SERIAL PRIMARY KEY,
    world_id         VARCHAR(64) NOT NULL REFERENCES worlds(id) ON DELETE CASCADE,
    from_phase       VARCHAR(32) NOT NULL,
    to_phase         VARCHAR(32) NOT NULL,
    trigger_type     VARCHAR(32) NOT NULL DEFAULT 'auto',  -- auto / manual / workflow_action
    triggered_by     VARCHAR(128),
    conditions_json  JSONB,              -- 转换时的条件状态快照
    created_at       TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_pipeline_history_world_time
    ON pipeline_history(world_id, created_at DESC);

CREATE OR REPLACE FUNCTION update_pipeline_updated_at()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER trigger_pipeline_updated_at
    BEFORE UPDATE ON pipeline_states
    FOR EACH ROW EXECUTE FUNCTION update_pipeline_updated_at();
```

## 8. 默认管线配置 — default_creative_pipeline.json

### 8.1 全局设置

```json
{
  "name": "default_creative_pipeline",
  "description": "标准5阶段创作管线：世界观→角色→情节→场景→反思",
  "version": 1,
  "auto_advance": true,
  "require_confirmation": false
}
```

### 8.2 阶段1：worldbuilding（世界观构建）

- **initial**: true
- **context inject**: phase_guidance, available_tools, world_summary
- **allowed_tools**: create_location, add_world_knowledge, create_character_card, update_world
- **advance_when** (AND):
  1. `entity_count` agent >= 2 — "至少创建 2 个角色"
  2. `world_has_rule_system` — "世界规则体系已建立"
  3. `entity_count` location >= 1 — "至少定义 1 个主要地点"
- **on_exit**: validate_world_completeness

### 8.3 阶段2：character_creation（角色创建）

- **context inject**: phase_guidance, world_summary, character_relations; extra: `include_existing_characters: true`
- **allowed_tools**: create_character_card, update_character_card, add_relation, add_character_diary, create_location
- **advance_when** (AND):
  1. `entity_count` agent (kind_filter=individual) >= 3 — "至少创建 3 个主要角色"
  2. `entity_count` relation >= 2 — "至少建立 2 条角色关系"
  3. `all_characters_have_cards` — "所有角色均有完整角色卡"
- **allowed_retreat**: ["worldbuilding"]

### 8.4 阶段3：plot_architecture（情节架构）

- **context inject**: phase_guidance, character_summary, plot_templates
- **allowed_tools**: create_arc, create_chapter, plant_foreshadowing, create_secret, add_timeline_event, update_character_card
- **advance_when** (AND):
  1. `entity_count` chapter >= 1 — "至少创建 1 个章节"
  2. `entity_count` foreshadowing >= 1 — "至少布置 1 个伏笔"
- **allowed_retreat**: ["character_creation"]

### 8.5 阶段4：scene_writing（场景写作）

- **context inject**: phase_guidance, current_scene_context, participant_cards, secret_knowledge_boundaries
- **allowed_tools**: create_scene, end_scene, update_foreshadow, add_diary, add_relation, voice_check
- **advance_when** (AND):
  1. `scene_count_in_chapter` >= $total_scenes_target — "当前章节场景数达标"
  2. `all_scenes_ended` — "所有场景已结束（日记+关系已更新）"
- **auto_loop**: entity=chapter, target=all_scenes_in_chapter, continue_while="scene_count < total_scenes_target"
- **allowed_retreat**: ["plot_architecture"]

### 8.6 阶段5：reflection（回顾整理）

- **context inject**: phase_guidance, chapter_summary, unresolved_foreshadowing, exposed_secrets
- **allowed_tools**: voice_check, update_foreshadow, review_chapter, update_character_card, add_memory_summary
- **advance_when** (AND):
  1. `all_checks_passed` (checks: character_consistency, plot_coherence, foreshadow_management, pacing) — "所有检查项目通过"
- **on_complete**: conditional → `has_more_chapters` ? goto_phase(scene_writing) : emit_sse(pipeline_cycle_complete)
- **allowed_retreat**: ["scene_writing"]

## 9. 集成点：需要修改的现有代码

### 9.1 cli/src/main.cpp — 初始化 PipelineManager

```cpp
auto pipeline_mgr = std::make_shared<worldbuilding::PipelineManager>(
    worldbuilding::PipelineManager::Dependencies{
        .pg_connection_factory = [&]{ return pg_pool->acquire(); },
        .event_emitter = [&runtime](const RuntimeEvent& e){
            runtime->emit_event(e.session_id, e.run_id, e.type, e.payload);
        },
        .pipeline_config_dir = config_dir / "pipelines",
    }
);
pipeline_mgr->initialize();
runtime_service->set_pipeline_manager(pipeline_mgr);
```

### 9.2 runtime_service.cpp — 事件桥接 + Context 注入 + Checkpoint

**事件桥接** — 在现有 domain 事件发射点之后调用：

```cpp
void RuntimeService::after_entity_event(const std::string& world_id,
                                        const std::string& event_type,
                                        const nlohmann::json& payload) {
    if (pipeline_mgr_) {
        pipeline_mgr_->on_world_event(world_id, event_type, payload);
    }
}
```

具体调用点：
- `emit("story_context_updated")` 之后
- `emit("scene_changed")` 之后
- `SceneOrchestrator::end_scene` 完成之后

**Context 注入** — 在 `build_prompt_profile()` / `execute_run()` 中：

```cpp
if (wb_service_ && pipeline_mgr_ && !session->world_id.empty()) {
    auto profile = build_prompt_profile(session->world_id, session->agent_id);
    profile.phase_context = pipeline_mgr_->get_phase_context(session->world_id);
    profile.phase_allowed_tools = pipeline_mgr_->get_allowed_tools(session->world_id);
    auto composed = compositor.assemble(profile);
    loop->set_system_prompt(composed);
}
```

**Checkpoint 集成** — 创建 RunCheckpoint 时：

```cpp
checkpoint.pipeline_snapshot_json = pipeline_mgr_->snapshot_to_json(world_id);
```

### 9.3 prompts/types.hpp — PromptProfile 增加字段

```cpp
struct PromptProfile {
    // ... 现有字段 ...
    std::string phase_context;                  // generate_phase_context() 的输出
    std::vector<std::string> phase_allowed_tools; // 当前阶段允许的工具
};
```

### 9.4 PipelineState 结构体扩展 — pipeline.hpp

在现有 `PipelineState` 中新增字段：

```cpp
struct PipelineState {
    // ... 现有字段不变 ...
    std::string active_workflow;      // 当前激活的工作流名称
    int chapter_count = 0;            // 已创建的章节总数
    int total_chapters_target = 0;    // 本章（或卷）的章节目标数
    bool is_cycle_complete = false;   // 是否完成了一轮完整循环
    int cycle_count = 0;              // 完成了几轮 scene_writing→reflection 循环
    nlohmann::json extra;             // 扩展字段，工作流可自定义
};
```

### 9.5 worldbuilding_http_handler.cpp — 4个新 REST 端点

#### GET `/api/worldbuilding/{id}/pipeline/state`

返回当前管线状态、条件进度、历史记录。响应格式：

```json
{
  "phase": "character_creation",
  "label": "角色创建",
  "conditions": [{"name": "...", "met": false, "current": 2, "target": 3}],
  "active_workflow": "default_creative_pipeline",
  "recent_history": [...],
  "next_allowed": ["plot_architecture"],
  "allowed_retreat": ["worldbuilding"]
}
```

#### POST `/api/worldbuilding/{id}/pipeline/advance`

手动推进/退回阶段。请求体：

```json
{
  "target_phase": "plot_architecture",  // 可选，不传则自动找下一个
  "force": false                         // true=跳过条件检查
}
```

Response: `{"ok": true}` 或 400 错误（含 `AdvanceResult` 字符串）。

#### GET `/api/worldbuilding/pipeline/workflows`

列出可用管线工作流。返回：

```json
{
  "workflows": [
    {"name": "default_creative_pipeline", "description": "...", "version": 1, "phase_count": 5}
  ]
}
```

#### POST `/api/worldbuilding/{id}/pipeline/activate`

激活指定工作流。请求体：`{"workflow_name": "custom_pipeline"}`

## 10. 前端修改设计

### 10.1 AppState 新增状态字段

```typescript
interface AppState {
  // ... existing fields ...
  pipelinePhase: string | null;  // 已有

  // ═══ 新增 ═══
  pipelineConditions: ConditionState[];
  pipelineActiveWorkflow: string;
  pipelineHistory: PhaseTransition[];
  pipelineNextAllowed: string[];
  pipelineAllowedRetreat: string[];
  pipelineAutoAdvance: boolean;
}

interface ConditionState {
  name: string;
  met: boolean;
  current?: number;
  target?: number;
}

interface PhaseTransition {
  id: string;
  from: CreativePhase;
  to: CreativePhase;
  trigger: string;
  timestamp: string;
}
```

### 10.2 SSE 事件处理

**完善 `pipeline_condition_progress`** — 更新 `pipelineConditions`，驱动前端进度条实时刷新。

**新增 `pipeline_condition_met`** — 当 `require_confirmation=true` 时弹出确认提示，包含阶段名、条件列表、确认/取消按钮。

**新增 `pipeline_cycle_complete`** — 显示 success toast "创作管线全周期完成"。

**完善 `pipeline_phase_changed`** — 同时更新 `pipelinePhase`、`pipelineConditions`、`pipelineNextAllowed`、`pipelineAllowedRetreat`。

### 10.3 PipelineNavigator 增强版

核心交互变化：
- **可点击的阶段节点**：逐阶段推进（不允许跳阶段），退回则任意之前阶段均可
- **handlePhaseClick()**：前进需 `window.confirm` + API 调用，退回自动 force=true
- **auto/manual 模式 badge**：显示在标题旁
- **条件进度条**（conditions-mini）：当前阶段下方显示小圆点，绿=已满足 / 灰=未满足
- **hover tooltip**：显示每个条件的详情（名称 + 当前值/目标值）
- **动画**：阶段切换时 ✓→● 过渡动画

### 10.4 WorkflowMonitor（新增组件）

工作流运行状态面板，显示：
- 当前阶段名称和描述
- 条件完成进度（进度条 + 百分比）
- 最近的转换历史（最近5条）
- auto/manual 切换开关

### 10.5 API Client 新增函数

```typescript
advancePipeline(worldId: string, body: { target_phase?: string; force?: boolean }): Promise<void>
getPipelineState(worldId: string): Promise<PipelineViewData>
listPipelineWorkflows(): Promise<WorkflowSummary[]>
activatePipelineWorkflow(worldId: string, workflowName: string): Promise<void>
```

### 10.6 types.ts 新增类型

```typescript
export interface ConditionState { name: string; met: boolean; current?: number; target?: number; }
export interface PhaseTransition { id: string; from: CreativePhase; to: CreativePhase; trigger: string; timestamp: string; }
export interface PipelineViewData { phase: CreativePhase; label: string; conditions: ConditionState[]; active_workflow: string; recent_history: PhaseTransition[]; next_allowed: CreativePhase[]; allowed_retreat: CreativePhase[]; }
export interface WorkflowSummary { name: string; description: string; version: number; phase_count: number; }
```

## 11. 测试策略

### 11.1 单元测试（GTest）

| 测试内容 | 说明 |
|----------|------|
| 9个 ConditionEvalFn 独立测试 | 用 mock PG connection 验证每类条件正确性 |
| PipelineWorkflowDef JSON 解析 | 验证 default_creative_pipeline.json 完整解析 |
| allowed_next_phases() 状态机 | 覆盖所有 11 条转换路径（含双向可逆边） |
| advance_phase 所有分支 | SUCCESS / INVALID_TRANSITION / CONDITIONS_NOT_MET / ALREADY_AT_PHASE / NO_ACTIVE_STATE |
| 防抖逻辑 | 验证 2s 窗口内重复事件被忽略 |

### 11.2 集成测试（GTest + 真实 PostgreSQL）

| 测试内容 | 说明 |
|----------|------|
| PipelineManager + PG 建表 | ensure_tables() 正确创建两张表 |
| 完整 5 阶段流转 | 创建 entity 触发条件满足 → 验证自动推进到下一阶段 |
| 手动推进 + 强制推进 | force=true 跳过条件检查 |
| 退回到上一阶段 | allowed_retreat 校验 |
| SSE 事件负载完整性 | pipeline_phase_changed 包含所有必需字段（phase, conditions, allowed_tools, next_allowed, allowed_retreat） |
| checkpoint 序列化/反序列化 | snapshot_to_json → restore_from_snapshot 往返一致性 |
| context 注入 | 验证 build_prompt_profile 产出的 system prompt 包含正确的 phase 内容 |

### 11.3 API 测试（GTest）

| 测试内容 | 说明 |
|----------|------|
| GET /pipeline/state | 正常返回 + world 不存在 404 |
| POST /pipeline/advance | 正常推进 + 条件不满足 + 非法转换 + 强制推进 |
| GET /pipeline/workflows | 返回至少包含 default_creative_pipeline |
| POST /pipeline/activate | 正常激活 + 工作流不存在 400 |

### 11.4 前端测试（Vitest）

| 测试内容 | 说明 |
|----------|------|
| PipelineNavigator 渲染 | 5 个阶段显示、当前阶段高亮、已完成阶段 ✓ |
| PipelineNavigator 交互 | 点击下一阶段弹出确认、点击已完成阶段可退回 |
| 条件进度条 | conditions-mini 正确显示 met/pending 状态 |
| AppState SSE 处理 | phase_changed / condition_progress / condition_met / cycle_complete |
| Toast 弹出 | cycle_complete 时的 success toast |
| WorkflowMonitor | 正确显示条件进度、历史记录 |

## 12. 架构原则

1. **观察者而非管理者** — PipelineManager 不管理实体（那是 WorldbuildingService 的职责），它观察已有系统的事件和数据，只负责管线流转。
2. **配置驱动** — 阶段定义、条件、动作全部通过 JSON 配置，不硬编码。可以通过添加 JSON 文件支持不同的创作流程。
3. **条件通过 PG 查询求值** — 不是内存比对，而是直接查询 PostgreSQL 中的权威数据。
4. **防抖保护** — 短时间内同一 world 的重复事件不会反复触发条件求值。
5. **双模式推进** — 自动模式（条件满足自动流转）+ 手动模式（用户确认后流转），可通过 JSON 和 API 动态切换。
6. **前后端对称** — 管线状态通过 SSE 实时推送到前端，也通过 REST 端点可供主动查询。前端 UI 完整反映后端状态。
