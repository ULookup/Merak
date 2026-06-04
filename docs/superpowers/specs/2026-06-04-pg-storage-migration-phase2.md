# PostgreSQL Storage Migration Phase 2 — P0+P1

## 目标

完成 WorldStore/AgentStore 迁移后的遗留工作：测试编译修复、混合搜索激活、剩余 3 个 Store 迁移到 PostgreSQL。

## 范围

- **P0**: 测试编译修复（所有测试适配新 PG 构造函数签名）
- **P1**: 混合搜索（C++ 侧调用已定义的 hybrid_search PL/pgSQL 函数）
- **P1**: NarrativeStore / ForeshadowingStore / SecretStore 迁移到 PG

**不在范围**: 数据迁移（SQLite 无数据）、WorldbuildingTools 改动（接口不变）、新的向量搜索功能

## 架构

```
                    WorldbuildingService(pg_conninfo, data_root)
                           |
         ┌─────────────────┼─────────────────────┐
         |                 |                     |
    WorldStore        AgentStore           NarrativeStore
    (已迁移)          (已迁移)             (待迁移)
                                               |
                         ┌─────────────────────┤
                         |                     |
                  ForeshadowingStore       SecretStore
                  (待迁移)                  (待迁移)
```

- 所有 Store 统一构造签名 `(pg_conninfo, data_root)`
- pg_conninfo 从 WorldbuildingService 逐层下传
- 接口零改动，工具层无感知
- PG 负责检索，文件负责可读；ForeshadowingStore 和 SecretStore 的 list/stats 类方法仍主走文件系统

## 模块 1: 测试编译修复（P0）

### 问题

Phase 1 迁移后 WorldStore、AgentStore、WorldbuildingService 构造函数增加了 `pg_conninfo` 参数。所有测试文件仍在用旧单参数签名，无法编译。

### 方案

1. 新建 `tests/test_helpers.hpp`，提供 `test_pg_conninfo()`：
   - 优先读环境变量 `MERAK_TEST_PG`
   - 无环境变量时返回 `"dbname=merak_test"`
2. 更新 6 个测试文件中所有 Store 和 Service 的构造调用
3. 移除不再需要的 `#include <merak/worldbuilding/sqlite_helpers.hpp>`

### 涉及文件

- 新建: `libs/worldbuilding/tests/test_helpers.hpp`
- 修改: `test_world_store.cpp`, `test_agent_store.cpp`, `test_narrative_store.cpp`, `test_foreshadowing_secret.cpp`, `test_scene_orchestrator.cpp`, `test_worldbuilding_e2e.cpp`

### 验收标准

- 所有测试文件编译通过（不要求运行通过——需要 PG 环境）

---

## 模块 2: 混合搜索激活（P1）

### 问题

schema.sql 已定义 `hybrid_search_knowledge()` 和 `hybrid_search_diary()` 两个 PL/pgSQL 函数（全文检索 + 向量相似度加权排序），但 C++ 侧 `search_world_knowledge()` 和 `search_diary()` 仍用纯 FTS + LIKE 回退。

### 方案

`world_store.cpp::search_world_knowledge`:
- 主路径改为 `SELECT * FROM hybrid_search_knowledge($1, $2, $3, $4)`
- 捕获异常后回退到 LIKE 搜索（FTS 扩展不可用时）

`agent_store.cpp::search_diary`:
- 主路径改为 `SELECT * FROM hybrid_search_diary($1, $2, $3, $4)`
- 同样异常回退 LIKE

接口签名、返回值类型完全不变。

### 涉及文件

- 修改: `libs/worldbuilding/src/world_store.cpp`, `libs/worldbuilding/src/agent_store.cpp`

### 验收标准

- 编译通过
- 搜索方法签名不变
- FTS 不可用时自动回退 LIKE

---

## 模块 3: NarrativeStore 迁移（P1）

### 问题

`narrative_store.cpp`（694 行）仍使用 SqliteDb 管理 5 个表：arcs、chapters、sections、scenes、timeline_events。需要替换为 PgConn。

### 方案

**DDL 新增**（追加到 schema.sql）:

```sql
CREATE TABLE IF NOT EXISTS arcs (
    id TEXT PRIMARY KEY, world_id TEXT NOT NULL REFERENCES worlds(id),
    name TEXT, description TEXT, theme TEXT, status TEXT,
    metadata JSONB DEFAULT '{}',
    created_at TIMESTAMPTZ DEFAULT now(), updated_at TIMESTAMPTZ DEFAULT now()
);
CREATE TABLE IF NOT EXISTS chapters (
    id TEXT PRIMARY KEY, world_id TEXT NOT NULL REFERENCES worlds(id),
    arc_id TEXT, name TEXT, pitch TEXT, status TEXT, position INT DEFAULT 0,
    created_at TIMESTAMPTZ DEFAULT now(), updated_at TIMESTAMPTZ DEFAULT now()
);
CREATE TABLE IF NOT EXISTS sections (
    id TEXT PRIMARY KEY, world_id TEXT NOT NULL REFERENCES worlds(id),
    chapter_id TEXT NOT NULL, name TEXT, status TEXT, position INT DEFAULT 0,
    created_at TIMESTAMPTZ DEFAULT now(), updated_at TIMESTAMPTZ DEFAULT now()
);
CREATE TABLE IF NOT EXISTS scenes (
    id TEXT PRIMARY KEY, world_id TEXT NOT NULL REFERENCES worlds(id),
    chapter_id TEXT, section_id TEXT, name TEXT, pitch TEXT, status TEXT,
    participants TEXT[] DEFAULT '{}',
    pov_character_id TEXT, location TEXT,
    world_time TIMESTAMPTZ, scene_time TIMESTAMPTZ,
    is_flashback BOOLEAN DEFAULT false, scene_index INT DEFAULT 0,
    created_at TIMESTAMPTZ DEFAULT now(), updated_at TIMESTAMPTZ DEFAULT now()
);
CREATE TABLE IF NOT EXISTS timeline_events (
    id TEXT PRIMARY KEY, world_id TEXT NOT NULL REFERENCES worlds(id),
    scene_id TEXT, event TEXT, world_time TIMESTAMPTZ,
    created_at TIMESTAMPTZ DEFAULT now()
);
```

**C++ 改动**:
- 构造函数增加 `pg_conninfo` 参数，新增 `std::unique_ptr<PgPool> pool_` 成员
- 移除 `database_path()` 方法和 `initialize()` 中 SQLite CREATE TABLE
- 所有方法中 `SqliteDb db(...)` → `PgConn conn(*pool_)`
- `execute_bound(sql, {params})` → `conn.execute(sql, {params})`
- `query_bound(sql, {params})` → `conn.query(sql, {params})` + 行遍历
- JSON 文件读写代码保持不变（主要存储路径）

**涉及 9 个公开方法 + initialize**:
`create_story_structure`, `create_arc`, `create_chapter`, `create_section`, `create_scene`, `update_scene_status`, `append_scene_text`, `record_timeline_event`, `advance_time`, `insert_flashback_scene`, `chapter_context`, `get_scene`

### 涉及文件

- 修改: `libs/worldbuilding/include/merak/worldbuilding/narrative_store.hpp`
- 修改: `libs/worldbuilding/src/narrative_store.cpp`
- 修改: `libs/worldbuilding/schema.sql`（追加 DDL）

### 验收标准

- 编译通过
- 接口不变，WorldbuildingTools 无改动
- JSON 文件读写行为不变

---

## 模块 4: ForeshadowingStore 迁移（P1）

### 问题

`foreshadowing_store.cpp`（413 行）用 SqliteDb 管理 1 个表。大部分读方法走文件系统，SQL 仅用于 status / created_by 查询。迁移量较小。

### 方案

**DDL 新增**:

```sql
CREATE TABLE IF NOT EXISTS foreshadowings (
    id TEXT PRIMARY KEY, world_id TEXT NOT NULL REFERENCES worlds(id),
    hint TEXT, hint_level TEXT DEFAULT 'subtle',
    status TEXT DEFAULT 'open', created_by TEXT DEFAULT 'author',
    pay_off_scene_id TEXT, pay_off TEXT,
    created_at TIMESTAMPTZ DEFAULT now(), updated_at TIMESTAMPTZ DEFAULT now()
);
```

**C++ 改动**:
- 构造函数增加 `pg_conninfo` 参数
- `plant()`: JSON 写文件（保留） + PG INSERT（替换 SQLite INSERT）
- `pay()` / `abandon()`: JSON 更新文件（保留） + PG UPDATE status
- `list()` / `stats()` / `chapter_summary()` / `final_act_reminders()`: 继续走文件系统，必要时 PG 查询 supplement
- `relevant_for_scene()`: 继续遍历文件目录

### 涉及文件

- 修改: `libs/worldbuilding/include/merak/worldbuilding/foreshadowing_store.hpp`
- 修改: `libs/worldbuilding/src/foreshadowing_store.cpp`
- 修改: `libs/worldbuilding/schema.sql`（追加 DDL）

---

## 模块 5: SecretStore 迁移（P1）

### 问题

`secret_store.cpp`（410 行）用 SqliteDb 管理 1 个表。模式与 ForeshadowingStore 完全一致——JSON 主存储，SQL 辅助。迁移量较小。

### 方案

**DDL 新增**:

```sql
CREATE TABLE IF NOT EXISTS secrets (
    id TEXT PRIMARY KEY, world_id TEXT NOT NULL REFERENCES worlds(id),
    secret_type TEXT DEFAULT 'background', status TEXT DEFAULT 'active',
    holder_ids TEXT[] DEFAULT '{}', known_by_ids TEXT[] DEFAULT '{}',
    content TEXT, stakes TEXT, deeper_truth TEXT,
    exposed_in_scene_id TEXT,
    created_at TIMESTAMPTZ DEFAULT now(), updated_at TIMESTAMPTZ DEFAULT now()
);
```

**C++ 改动**:
- 构造函数增加 `pg_conninfo` 参数
- `create()`: JSON 写文件（保留） + PG INSERT
- `transfer()` / `expose()` / `abandon()` / `reverse_truth()`: JSON 更新 + PG UPDATE
- `list()` / `scene_asymmetry()` / `check_leak_risk()`: 继续走文件系统

### 涉及文件

- 修改: `libs/worldbuilding/include/merak/worldbuilding/secret_store.hpp`
- 修改: `libs/worldbuilding/src/secret_store.cpp`
- 修改: `libs/worldbuilding/schema.sql`（追加 DDL）

---

## 连锁影响

- `WorldbuildingService` 构造函数将 pg_conninfo 传递给 NarrativeStore、ForeshadowingStore、SecretStore
- ForeshadowingStore 依赖 NarrativeStore，SecretStore 依赖 ForeshadowingStore，传递链保持
- SceneOrchestrator 只持有 Store 引用，不构造它们，因此自身无需修改

## 改动汇总

| 模块 | 新建 | 修改 | C++ 改动量 | SQL 改动量 |
|------|:--:|:--:|-----------|-----------|
| 测试编译 | 1 | 6 | ~100 行 | 0 |
| 混合搜索 | 0 | 2 | ~40 行 | 0 |
| NarrativeStore | 0 | 3 | ~200 行 | ~80 行 |
| ForeshadowingStore | 0 | 2 | ~50 行 | ~15 行 |
| SecretStore | 0 | 2 | ~50 行 | ~15 行 |
| WorldbuildingService | 0 | 2 | ~20 行 | 0 |
| SceneOrchestrator | 0 | 1 | ~10 行 | 0 |

**总计**: 1 新建, 18 修改, ~470 行 C++, ~110 行 SQL
