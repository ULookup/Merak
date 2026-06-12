# 死代码清理与接线

> 状态：已确认
> 日期：2026-06-12

---

## 1. 总览

清理 18 项已确认的死代码：完整实现但零调用、从未初始化的成员、重复定义、已废弃的类。按处理方式分四组。

| 组 | 处理方式 | 项数 |
|---|---|---|
| A | 纯删除 / 提取共享 | 6 |
| B | 简单接线（已有调用点，补调用） | 6 |
| C | 需要设计协调（多模块改动） | 3 |
| D | Tool Redesign spec 已覆盖 | 2 |

---

## 2. A 组：纯删除 / 提取共享

### 2.1 #38 — 删除 ToolResultCompactor

**理由**：设计文档明确标为 deprecated，功能已被 `ContextOptimizer::microcompact()` 替代。

**操作**：
- 删除 `libs/context/include/merak/tool_result_compactor.hpp`
- 删除 `libs/context/src/tool_result_compactor.cpp`
- 从 `libs/context/CMakeLists.txt` 移除 `src/tool_result_compactor.cpp`

### 2.2 #41 — 删除 CacheAwareContext::append()

**理由**：一行 `messages.push_back(new_msg)` 对 `std::vector` 的无意义包装，零调用者。

**操作**：
- 删除 `libs/context/src/cache_aware_context.cpp:37-42` 方法体
- 删除 `libs/context/include/merak/cache_aware_context.hpp` 中的声明

### 2.3 #44 — 删除 MemoryStore::db_conn_ 成员

**理由**：`std::string db_conn_` 仅在头文件声明，`.cpp` 中从未初始化、从未使用。实际的数据库连接通过 `MemoryConfig::db_connection` 在 `init_db()` 中建立。

**操作**：
- 删除 `libs/memory/include/merak/memory_store.hpp:69`

### 2.4 #47 — 删除未使用的 WebUI types

**理由**：`ReviewIssue`、`ReviewSummary`、`PipelineState` 在生产代码中零引用。`PipelineNavigator` 实际使用 `PipelineViewData`（不同接口）。`MemorySummary` 保留（AgentsInspector TODO 标注了未来用途）。

**操作**：
- 删除 `webui/src/api/types.ts` 中的 `ReviewIssue`、`ReviewSummary`、`PipelineState` 及其关联的子接口
- 保留 `MemorySummary` 和 `MemorySummaryListResponse`

### 2.5 #48 — 提取 remove_all_no_throw 消除重复

**理由**：`agent_store.cpp:100` 和 `world_store.cpp:70` 中各有一个 `namespace {}` 内的完全相同的 3 行函数。

**操作**：
- 在 `libs/worldbuilding/include/merak/worldbuilding/ids.hpp` 中新增 `inline void remove_all_no_throw(const std::filesystem::path&) noexcept`
- 删除 `agent_store.cpp` 和 `world_store.cpp` 中的本地定义
- 两处调用点保持不变

---

## 3. B 组：简单接线

### 3.1 #37 — 接入 worldbuilding prompt loaders

**现状**：`libs/worldbuilding/src/prompts/` 下 `load_character_prompt()`、`load_creative_director()`、`load_domain_manager_prompt()` 三个 loader 完整实现，对应 `config/prompts/worldbuilding/` 下实际存在的 markdown 文件。零调用。

**接线**：`SceneOrchestrator::prepare_scene()` 在构建 `CharacterContextView` 时，根据 `AgentKind` 调用对应 loader：

| AgentKind | Loader | Prompt 文件 |
|---|---|---|
| Character / Individual / Group | `load_character_prompt()` | `character.md` |
| God | `load_creative_director()` | `creative_director.md` |
| Manager (各子类型) | `load_domain_manager_prompt()` | `domain_manager.md` |

获取的 prompt 文本追加到 `CharacterContextView` 的 `behavior_constraints` 字段。

**路径解析**：loader 默认参数是相对路径 `"config/prompts"`，调用方需传入从 exe 目录解析的绝对路径（与 `main.cpp` 中 pipeline config 路径解析方式一致：`exe_dir / ".." / "config" / "prompts"`）。

### 3.2 #35 — 接入 unregister_session_world / world_session_count

**现状**：`RuntimeService::unregister_session_world()` 和 `world_session_count()` 完整实现，零调用。

**接线**：
- `unregister_session_world(session_id)` → 在 `RuntimeService::destroy_session()` 末尾调用
- `world_session_count(world_id)` → 在 `WorldbuildingHttpHandler` 世界列表接口中返回每个 world 的活跃会话数

### 3.3 #39 — 接入 escalate_for_recovery

**现状**：`ContextPipeline::escalate_for_recovery()` 只有一个 stats 记录，零调用。

**接线**：
- `ContextPlanner` 在选到 `AggressivePrune` 等级时调用 `escalate_for_recovery()`
- 扩展方法体：在记录 stats 之前触发 spill 将所有可溢出内容写入磁盘

### 3.4 #43 — 接入 classify_error

**现状**：`TurnIngestor::classify_error(http_status, body_hint)` 完整实现，零调用。

**接线**：`AgentLoop` 在 LLM HTTP 调用返回错误时调用此方法，按结果决定策略：

| LlmErrorClass | 策略 |
|---|---|
| Auth | 立即 fail，错误信息提示用户检查 API key |
| RateLimit | 指数退避重试（最多 3 次，间隔 1s/2s/4s） |
| ContextWindow | 触发 `Compactor::compact()` 后重试 1 次 |
| Unknown | 记录 spdlog::warn + fail |

### 3.5 #41 — 接入 will_cache_hit

**现状**：`CacheAwareContext::will_cache_hit(prev, curr)` 完整实现，零调用。

**接线**：`ContextPipeline::planned_assemble()` 结束时，对比本轮 split 与上轮 split，结果计入 `PipelineStats` 缓存命中率字段。

### 3.6 #40 — 接入 compact_one_round

**现状**：`Compactor::compact_one_round(round_messages)` 完整实现，零调用。

**接线**：`ContextOptimizer::drop_rounds()` 中，对被 drop 的每一轮消息调用 `compact_one_round()` 生成单轮摘要（而非直接丢弃），摘要追加到压缩缓冲区。

---

## 4. C 组：需要设计协调

### 4.1 #33 — SessionStore 全面切换 PostgreSQL

**现状**：`libs/storage/` 下有 SQLite 版 `SessionStore`（已使用）和 PostgreSQL 版 `SessionStorePG`（598 行，从未实例化）。

**设计**：删除 SQLite 版，`SessionStorePG` 重命名为 `SessionStore`，成为唯一实现。

```
之前: RuntimeService → SessionStore (SQLite)
之后: RuntimeService → SessionStore (PostgreSQL, 原 SessionStorePG)
```

**操作**：
- 删除 `libs/storage/src/session_store.cpp`（SQLite 实现）
- 删除 `libs/storage/include/merak/session_store.hpp`（SQLite 接口）
- `session_store_pg.hpp` → 重命名为 `session_store.hpp`
- `session_store_pg.cpp` → 重命名为 `session_store.cpp`
- 类名 `SessionStorePG` → `SessionStore`（保持公开接口不变）
- 更新 `RuntimeService`、`cli/src/main.cpp`、`tests/` 中的 include 和构造调用

### 4.2 #34 — 接入 checkpoint 崩溃恢复

**现状**：`RunCheckpoint` 和 `ToolCallRecord` 在 `libs/runtime/include/merak/checkpoint.hpp` 中定义，仅被 `SessionStorePG` 头文件引用。零运行时使用。

**设计**：checkpoint 类型提升为通用类型，接入 AgentLoop 崩溃恢复链路。

**类型移动**：`RunCheckpoint` 和 `ToolCallRecord` 从 `libs/runtime/include/merak/checkpoint.hpp` 移到 `libs/core/include/merak/checkpoint.hpp`（storage、runtime、loop 三个模块都需要引用，放在 core 最合适）。

**AgentLoop 接线**：每轮 turn 结束后：

```cpp
auto checkpoint = RunCheckpoint{
    .run_id = run_id,
    .turn_index = turn_index,
    .turn_state = serialize(current_turn_state),
    .input_tokens_used = usage.input_tokens,
    .output_tokens_used = usage.output_tokens,
    .pending_calls = pending_tool_calls,
    .compacted_history_summary = compactor->last_summary(),
    .pipeline_snapshot_json = pipeline_stats.to_json().dump()
};
runtime_service->save_checkpoint(checkpoint);
```

**恢复接线**：`RuntimeService::resume_after_restarted_approval()` 先尝试从最新 checkpoint 的 `turn_state` 反序列化恢复 `TurnState`，失败时才回退到从事件重放。

### 4.3 #36 — 接入 create_story_structure

**现状**：`NarrativeStore::create_story_structure(world_id, template_type)` 完整实现（支持 ThreeAct / FourAct / HerosJourney / Freeform），仅在测试中调用。

**接线**：`PipelineManager` 执行 Worldbuilding 阶段（阶段 1）时，检查世界的 `story_structure.json` 是否存在：
- 不存在 → 调用 `create_story_structure(world_id, ThreeAct)` 创建默认三幕结构
- 存在 → 跳过

用户后续可通过 WebUI PipelineNavigator 切换模板类型。

---

## 5. D 组：Tool Redesign spec 已覆盖

| # | 项 | 状态 |
|---|-----|------|
| 45 | `BashTool::check_dangerous` | 当前代码中已不存在此函数。可能是早期重构中已删除 |
| 46 | `EditJournal::rollback` | 完整实现，零调用。接入方案已在 `2026-06-12-tool-module-redesign.md` 中定义：接入 `SessionTool::execute("rollback")` |

D 组不在本次清理范围，由 Tool Redesign 的 implementation plan 覆盖。

---

## 6. 改动范围

### 删除的文件

| 文件 | 原因 |
|------|------|
| `libs/context/include/merak/tool_result_compactor.hpp` | 废弃，功能已被 ContextOptimizer 替代 |
| `libs/context/src/tool_result_compactor.cpp` | 同上 |
| `libs/storage/src/session_store.cpp` | SQLite 版废弃，切换为 PG 版 |
| `libs/storage/include/merak/session_store.hpp` | 同上 |

### 重命名的文件

| 原路径 | 新路径 |
|------|------|
| `libs/storage/include/merak/session_store_pg.hpp` | `libs/storage/include/merak/session_store.hpp` |
| `libs/storage/src/session_store_pg.cpp` | `libs/storage/src/session_store.cpp` |
| `libs/runtime/include/merak/checkpoint.hpp` | `libs/core/include/merak/checkpoint.hpp` |

### 修改的文件

| 文件 | 变更 |
|------|------|
| `libs/context/CMakeLists.txt` | 移除 `tool_result_compactor.cpp` |
| `libs/context/include/merak/cache_aware_context.hpp` | 删 `append()` 声明 |
| `libs/context/src/cache_aware_context.cpp` | 删 `append()` 定义 |
| `libs/context/src/context_pipeline.cpp` | 接入 `will_cache_hit`；接入 `escalate_for_recovery` |
| `libs/context/include/merak/context_pipeline.hpp` | `escalate_for_recovery` 改签名（可选） |
| `libs/context/src/context_planner.cpp` | AggressivePrune 下调用 `escalate_for_recovery` |
| `libs/context/src/context_optimizer.cpp` | `drop_rounds` 调用 `compact_one_round` |
| `libs/memory/include/merak/memory_store.hpp` | 删 `db_conn_` 成员 |
| `libs/loop/src/agent_loop.cpp` | LLM error path 调用 `classify_error`；每轮结束调用 checkpoint 保存 |
| `libs/runtime/src/runtime_service.cpp` | 构造改为 PG SessionStore；`destroy_session` 调 `unregister_session_world`；checkpoint 保存/恢复 |
| `libs/runtime/include/merak/runtime_service.hpp` | SessionStore 类型变更 |
| `libs/storage/CMakeLists.txt` | 更新编译文件 |
| `libs/worldbuilding/src/scene_orchestrator.cpp` | 接入 prompt loaders |
| `libs/worldbuilding/src/agent_store.cpp` | `remove_all_no_throw` 改为调用 ids.hpp 公共版 |
| `libs/worldbuilding/src/world_store.cpp` | 同上 |
| `libs/worldbuilding/include/merak/worldbuilding/ids.hpp` | 新增 `remove_all_no_throw` 声明 |
| `libs/worldbuilding/src/pipeline_manager.cpp` | 阶段 1 检查并创建 story_structure |
| `libs/http/src/worldbuilding_http_handler.cpp` | 世界列表返回活跃会话数 |
| `webui/src/api/types.ts` | 删 `ReviewIssue`、`ReviewSummary`、`PipelineState` |
| `cli/src/main.cpp` | 更新 SessionStore include 和构造 |
| `tests/` | 更新 SessionStore include |

---

## 7. 测试

| 场景 | 验证方式 |
|------|---------|
| SessionStore 切换后 serve 启动 | 启动 `merak serve`，创建 session、run，确认无报错 |
| checkpoint 保存和恢复 | 模拟崩溃：启动 run → 中途 kill serve → 重启 → 确认 session 可恢复 |
| classify_error 各分支 | 单元测试：mock 各 HTTP status 错误，验证分类和策略选择 |
| story_structure 自动创建 | 创建新 world → 确认 `story_structure.json` 自动生成 |
| compact_one_round 接入 | 高 token 压力场景，确认 drop_rounds 生成摘要而非直接丢弃 |
| prompt loader 接线 | 创建不同类型 Agent → 确认 prompt 文件内容出现在 system instruction 中 |
