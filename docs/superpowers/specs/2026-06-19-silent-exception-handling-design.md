# 异常静默处理 — 设计规格

## 背景

代码审计发现 31 处 `catch(...)` 块静默丢弃异常，分布在 8 个文件中，分三类：

| 类别 | 数量 | 风险 |
|---|---|---|
| 事务 ROLLBACK 二次异常 | 6 | 连接状态不可知，问题无法追踪 |
| 最佳努力/非关键操作 | 14 | 运维无感知，故障盲区 |
| JSON/数值解析防御 | 11 | 用异常做控制流，catch(...) 范围过宽 |

## 策略

工业后端标准：**非关键路径可以吞异常，但不能吞信号**。三类操作区分处理：

- **L1 事务 ROLLBACK**：记 error/critical 日志后 rethrow，不影响原始异常传播
- **L2 最佳努力**：`catch(...)` → `catch(const std::exception&)`，记 debug 级别日志
- **L3 解析防御**：引入 `safe_stoi`/`safe_json_parse` 返回 `std::optional`，消除 `catch(...)`

不新增依赖、不改接口、不改数据结构。每处改 2-4 行。

---

## L1 — 事务 ROLLBACK 异常（6 处）

### 模式

```cpp
// 改前：
} catch (...) {
    try { conn.exec("ROLLBACK"); } catch (...) {}
    throw;
}

// 改后：
} catch (const std::exception& e) {
    spdlog::error("<操作名> failed: {}", e.what());
    try { conn.exec("ROLLBACK"); } catch (const std::exception& re) {
        spdlog::critical("ROLLBACK also failed: {}", re.what());
    }
    throw;
}
```

### 位置

| 文件 | 行号 | 操作 |
|---|---|---|
| `libs/worldbuilding/src/secret_store.cpp` | 154 | `store()` |
| `libs/worldbuilding/src/secret_store.cpp` | 200 | `expose()` |
| `libs/worldbuilding/src/world_store.cpp` | 163 | `create_world()` |
| `libs/worldbuilding/src/world_store.cpp` | 240 | `delete_world()` |
| `libs/worldbuilding/src/narrative_store.cpp` | 361 | `create_chapter()` |
| `libs/worldbuilding/src/narrative_store.cpp` | 453 | `create_scene()` |

---

## L2 — 最佳努力操作（14 处）

### 模式 A：纯吞异常（加日志）

```cpp
// 改前：
} catch (...) {}

// 改后：
} catch (const std::exception& e) {
    spdlog::debug("<描述>: {}", e.what());
}
```

### 模式 B：有 fallback 逻辑（加日志后继续 fallback）

```cpp
// 改前：scene_orchestrator.cpp:298
} catch (...) {
    prompt << "代理人: " << pid << "\n";
}

// 改后：
} catch (const std::exception& e) {
    spdlog::debug("load_character_card({}) skipped: {}", pid, e.what());
    prompt << "代理人: " << pid << "\n";
}
```

### 位置

| 文件 | 行号 | 操作 | 模式 |
|---|---|---|---|
| `libs/worldbuilding/src/scene_orchestrator.cpp` | 263 | get_agent for KG names | A |
| `libs/worldbuilding/src/scene_orchestrator.cpp` | 273 | KG query_subgraph | A |
| `libs/worldbuilding/src/scene_orchestrator.cpp` | 298 | load_character_card fallback | B |
| `libs/worldbuilding/src/scene_orchestrator.cpp` | 334 | diary index loading | A |
| `libs/worldbuilding/src/scene_orchestrator.cpp` | 343 | shared memory refs | A |
| `libs/worldbuilding/src/scene_orchestrator.cpp` | 410 | voice fingerprint update | A |
| `libs/worldbuilding/src/scene_orchestrator.cpp` | 505 | group member card load | B |
| `libs/worldbuilding/src/scene_orchestrator.cpp` | 519 | individual card load | B |
| `libs/worldbuilding/src/scene_orchestrator.cpp` | 434 | foreshadowing proposal plant | A |
| `libs/worldbuilding/src/scene_orchestrator.cpp` | 587 | LLM compaction JSON parse | B |
| `libs/worldbuilding/src/secret_store.cpp` | 212 | foreshadowing pay (explicitly best-effort) | A |
| `libs/prompts/src/compositor.cpp` | 150 | context DSL resolution fallback | B |
| `libs/worldbuilding/src/world_store.cpp` | 108 | migration ALTER TABLE (幂等) | A* |
| `libs/worldbuilding/src/world_store.cpp` | 318 | hybrid_search_knowledge fallback to LIKE | A |

\* `world_store.cpp:108` 保留 `catch(...)`，注释说明 PG < 9.6 兼容原因

---

## L3 — 解析防御（11 处）

### 新增工具函数

放在 `libs/core/include/merak/utilities.hpp`（如该文件不存在则新建，放在 `libs/core/` 中）：

```cpp
// libs/core/include/merak/utilities.hpp
#pragma once

#include <string>
#include <optional>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace merak {

inline std::optional<int> safe_stoi(const std::string& s) {
    try { return std::stoi(s); }
    catch (const std::exception& e) {
        spdlog::debug("safe_stoi('{}'): {}", s, e.what());
        return std::nullopt;
    }
}

inline std::optional<nlohmann::json> safe_json_parse(const std::string& s) {
    try { return nlohmann::json::parse(s); }
    catch (const std::exception& e) {
        spdlog::debug("safe_json_parse failed for '{}...': {}",
              s.substr(0, 80), e.what());
        return std::nullopt;
    }
}

} // namespace merak
```

### 位置

| 文件 | 行号 | 改前 | 改后 |
|---|---|---|---|
| `libs/worldbuilding/src/world_store.cpp` | 48 | `try { json::parse(...); } catch(...) {}` | `meta.config = safe_json_parse(config_str).value_or(nlohmann::json{});` |
| `libs/worldbuilding/src/world_store.cpp` | 72 | `try { json::parse(...).get<...>(); } catch(...) {}` | `if (auto j = safe_json_parse(res.get(row, 4))) wk.tags = j->get<std::vector<std::string>>();` |
| `libs/worldbuilding/src/world_store.cpp` | 73 | 同上 | `if (auto j = safe_json_parse(res.get(row, 5))) wk.aliases = j->get<std::vector<std::string>>();` |
| `libs/worldbuilding/src/world_store.cpp` | 74 | 同上 | `if (auto j = safe_json_parse(res.get(row, 6))) wk.related_ids = j->get<std::vector<std::string>>();` |
| `libs/loop/src/agent_loop.cpp` | 604 | `try { json::parse(...); } catch(...) {}` | `auto j = safe_json_parse(result.output); if (j && j->value("status","")=="pending") ...` |
| `libs/loop/src/agent_loop.cpp` | 613 | 同上 | 同上 |
| `libs/worldbuilding/src/narrative_store.cpp` | 852 | `try { json::parse(...) } catch(...) { = {} }` | `auto j = safe_json_parse(res.get(i, 5)); scene.participant_ids = j ? j->get<std::vector<std::string>>() : std::vector<std::string>{}` |
| `libs/context/src/spill_store.cpp` | 55 | `try { std::stoi(...); } catch(...) {}` | `if (auto v = safe_stoi(stem_a.substr(0, us_a))) ta = *v;` |
| `libs/context/src/spill_store.cpp` | 58 | 同上 | `if (auto v = safe_stoi(stem_b.substr(0, us_b))) tb = *v;` |
| `libs/context/src/spill_store.cpp` | 120 | `try { std::stoi(...); } catch(...) {}` | `if (auto v = safe_stoi(stem.substr(0, underscore))) { if (*v < turn_index) ... }` |
| `libs/context_dsl/src/resolver.cpp` | 442 | `try { std::stoi(...); } catch(...) { limit = 5; }` | `limit = safe_stoi(limit_str).value_or(5);` |

---

## 不做什么

- 不改 `PgPool`/`PgConn` 接口（`acquire()` 已有健康检查，坏连接归还后下次自动替换）
- 不加 metrics 计数器框架（后续单独设计 metrics 系统）
- 不加 `CompoundError` 类型（ROLLBACK 失败后仍 rethrow 原始异常）
- 不引入任何新库依赖

## 测试

每个改动点不需要独立单元测试（改动量过小，风险低）。以下检查足够：

1. CI 编译通过（GCC14 + Clang18，Debug + Release）
2. 现有 `worldbuilding_tests` / `pipeline_tests` 全部通过
3. 对 L2（scene_orchestrator）, 手动生成一个 narrative 做冒烟测试，确认非关键路径异常不会导致崩溃
