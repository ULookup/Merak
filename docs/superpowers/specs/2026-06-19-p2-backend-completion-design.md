# P2 后端补全设计

## 范围

补全 Inspector 面板所需的结构化数据查询端点 + Pipeline 控制 + World 导入/导出。共 5 个模块 19 个端点，全部同步 CRUD，直接调已有 Store 方法。

Merak 是对话驱动的创作工作台。创作行为（写作、生成、重写）走 Session → Run → AgentLoop，REST 端点服务于 WebUI Inspector 面板（展示结构化数据）和配置控制。

## 代码变更范围

- `libs/http/src/worldbuilding_http_handler.cpp` — 所有新 handler
- `libs/http/include/merak/worldbuilding_http_handler.hpp` — 新增 handler 声明

---

## 模块 B：Pipeline 补全（2 端点）

### 设计

PipelineManager 已有 5 个端点（state、advance、workflows、history、activate）。补全 retreat 和 clear-error。

### 端点

```
POST /api/worldbuilding/:wid/pipeline/retreat
Body: { to_phase: "outlining|drafting|revising|polishing" }
→ 200 { ok, state: { phase, label, conditions, ... } }
// 回退到指定阶段

POST /api/worldbuilding/:wid/pipeline/clear-error
→ 200 { ok }
// 清除上一步错误，恢复可推进状态
```

### 实现

`PipelineManager` 已有 `handle_advance_failure` 和 `clear_last_error`。retreat 需新增 `retreat_to_phase` 方法：重置目标阶段的所有条件状态，回退 `current_phase`。

---

## 模块 C：日记/记忆/关系 API（8 端点）

### 设计

AgentStore 已有完整 CRUD。暴露为 Inspector 数据查询端点。

### 端点

#### 日记

```
GET /api/worldbuilding/:wid/agents/:aid/diaries/:did
→ 200 { ok, diary: { id, agent_id, scene_id, world_time, content, mood, status, leak_risk_level, tokens_used, created_at } }

PATCH /api/worldbuilding/:wid/agents/:aid/diaries/:did
Body: { content?, mood?, status? }
→ 200 { ok }

GET /api/worldbuilding/:wid/agents/:aid/diaries/search?q=...
→ 200 { ok, diaries: [...], total }
// 全文搜索日记内容
```

#### 记忆摘要

```
GET /api/worldbuilding/:wid/agents/:aid/memory-summaries
→ 200 { ok, summaries: [{ id, period_start, period_end, summary, source_diary_ids, created_at }] }

GET /api/worldbuilding/:wid/agents/:aid/memory-summaries/:mid
→ 200 { ok, summary: { id, period_start, period_end, summary, source_diary_ids, created_at } }
```

#### 关系

```
POST /api/worldbuilding/:wid/agents/:aid/relations
Body: { target_id, relation_type, description, intimacy? }
→ 201 { ok }

PATCH /api/worldbuilding/:wid/agents/:aid/relations/:tid
Body: { relation_type?, description?, intimacy? }
→ 200 { ok }
// upsert_relations 已支持覆盖更新
```

#### 声音指纹

```
GET /api/worldbuilding/:wid/agents/:aid/voice
→ 200 { ok, voice: { avg_sentence_length, sentence_variance, question_frequency, modifier_ratio, sample_count, signature_words, tone_profile } }
```

### 实现

全部直接调 AgentStore 已有方法。无 Store 变更。

---

## 模块 D：World 导入/导出（2 端点）

### 设计

全量快照导出为单一 JSON（`share-snapshot-v1` 格式），导入时全量 ID 重映射创建新 World。作为社区分享功能的前置基础。

### 端点

```
POST /api/worldbuilding/:wid/export-full
Body: { include_diaries?: false, include_memories?: false }
→ 200 { ok, snapshot: { schema_version: "1.0", exported_at, snapshot_id, source: { world_id, name }, manifest: { agent_count, chapter_count, scene_count, ... }, payload: { world, agents: [{ record, card, group_profile?, manager_profile? }], chapters, scenes, arcs, locations, factions, knowledge, foreshadowing, secrets, timeline, images: [{ id, agent_id, image_type, mime_type, original_name, is_primary, data: "base64..." | null }] } } }

POST /api/worldbuilding/import
Body: { snapshot: {...}, target_name? }
→ 201 { ok, world_id, id_mapping: { old_id: new_id, ... } }
```

### Snapshot 图片格式

- ≤256KB → `data` 字段 base64 内嵌
- >256KB → `data` 为 null，标记 `"transfer": "external"`
- 导入时 base64 小图直接写本地 ImageService，大图跳过

### ID 重映射

- 导入时所有 ID 走 `make_id()` 生成新 ID
- 维护 `id_mapping` 表（旧 ID → 新 ID），存入 `world.config._import_meta`
- 交叉引用字段（`participant_ids`, `scene_ids`, `member_agent_ids`, `rival_faction_ids`, `foreshadowing_ids`, `related_ids` 等）全部重映射
- `fork_chain` 数组记录派生链：`[{world_id, snapshot_id, author, imported_at}]`

### 实现

`WorldbuildingService` 新增 `export_world_snapshot()` 和 `import_snapshot()` 方法。Store 层无变更（导出走现有 get/list 方法，导入走现有 create 方法 + ID 替换）。

---

## 模块 E：知识图谱查询（4 端点）

### 设计

纯查询端点，从 KG Provider 读取实体和关系。不触发 extraction（extraction 由 GodAgent 在对话内完成）。

### 端点

```
GET /api/worldbuilding/:wid/knowledge-graph/entities?type=agent|location|faction|knowledge
→ 200 { ok, entities: [{ id, name, type, properties }] }

GET /api/worldbuilding/:wid/knowledge-graph/entities/:eid/relations
→ 200 { ok, entity: { id, name }, relations: [{ relation_type, target_id, target_name, target_type }] }

GET /api/worldbuilding/:wid/knowledge-graph/search?q=...&type=...
→ 200 { ok, results: [{ id, name, type }] }

GET /api/worldbuilding/:wid/scenes/:sid/extraction-result
→ 200 { ok, candidates: [{ relation: { subject_id, predicate, object_id }, status, evidence, change_summary }] }
// 某场戏的 KG extraction 候选结果
```

### 实现

通过 `WorldbuildingService::kg_provider()` 访问 KG。ExtractionResult 已有 `ExtractionService` 产出并存储。

---

## 模块 F：时间线 API（3 端点）

### 设计

整合现有的 `time_now` + `time_advance` 进入 timeline 框架，新增 event 查询。

### 端点

```
GET /api/worldbuilding/:wid/timeline
→ 200 { ok, current_time: { day, period, label }, events: [{ id, world_time, description, recorded_by, affected_character_ids, related_scene_ids }] }

GET /api/worldbuilding/:wid/timeline/events/:eid
→ 200 { ok, event: { id, world_time, description, recorded_by, recorded_by_name, affected_character_ids, related_scene_ids } }

POST /api/worldbuilding/:wid/timeline/advance
Body: { time_label: "第3日晚" | "day3_evening" | "2d" | "4h" }
→ 200 { ok, new_time: { day, period, label } }
// 推进世界时间，替代当前 time_advance 端点
```

### 实现

`NarrativeStore` 已有 `record_timeline_event`、`advance_time`。需新增 `list_timeline_events(world_id)` 和 `get_timeline_event(world_id, event_id)` 方法。

`WorldTime::parse()` 已实现，支持中英文格式 + duration 简写（`2d`, `4h`）。

---

## 汇总

| 模块 | 端点数 | Store 变更 | 基础设施依赖 |
|------|--------|-----------|-------------|
| B. Pipeline 补全 | 2 | 1 个新方法 `retreat_to_phase` | PipelineManager |
| C. 日记/记忆/关系 | 8 | 无 | AgentStore 已有方法 |
| D. World 导入/导出 | 2 | 2 个新方法 `export_world_snapshot` `import_snapshot` | WorldbuildingService |
| E. 知识图谱查询 | 4 | 无 | KG Provider |
| F. 时间线 | 3 | 2 个新方法 `list_timeline_events` `get_timeline_event` | NarrativeStore |
| **合计** | **19** | **5 个新方法** | — |

P0（~30）+ P1（18）+ P2（19）= **67 个端点**。

## 设计决策

- 全部同步 CRUD，不走 GodAgent 异步流程
- WriterAgent 增强不属于此次范围——它是对话层功能，非 REST 端点
- Snapshot 格式预留 `fork_chain` 和 `id_mapping`，为后续社区分享打基础
- 模块 E（KG 查询）仅查询，extraction 由 GodAgent 在对话内触发
