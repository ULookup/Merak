# ISSUE #171 修复：孤儿 tool_result 导致 Anthropic API 400

**日期**：2026-06-22
**ISSUE**：[#171](https://github.com/anthropics/merak/issues/171) — LLM API 400: orphaned tool_result blocks in Anthropic request — tool_use_id mismatch

## 问题陈述

Anthropic API 返回 HTTP 400：

```
unexpected `messages.0.content.0: tool_use_id` found in `tool_result` blocks:
call_00_xxx. Each `tool_result` block must have a corresponding `tool_use`
block in the previous message.
```

### 根因

两个代码路径会破坏 `tool_use` / `tool_result` 配对不变量：

1. **`MemoryStore::recent_history()`**（`libs/memory/src/memory_store.cpp:28-38`）  
   用 `max_turns * 2` 估算窗口大小，假设每轮 2 条消息。含工具调用的轮次实际 4–10+ 条（`user → assistant(tool_use) → tool_result × N`）。窗口可能切在 `assistant(tool_use)` 与 `tool_result` 之间，返回的 history 以孤儿 `tool` 消息开头。

2. **`ContextPipeline::planned_assemble()` hard trim**（`libs/context/src/context_pipeline.cpp:85-101`）  
   逐条 `erase` 消息，若删到含 `tool_use` 的 assistant 消息，后续 `tool_result` 变孤儿。

序列化器 `ContextSerializer::serialize()`（`libs/context/src/context_serializer.cpp`）将孤儿 `tool` 消息转为 Anthropic `tool_result` block，出现在 `messages[0]`，API 校验失败。

## 主流 Agent 实现参考

| Agent | 机制 |
|---|---|
| **Claude Code** | 对话以 turn 为单元存储（user + assistant + 所有 tool_use/tool_result）。截断时整轮原子删除。发送前校验 tool_use/tool_result 配对，孤儿 tool_result 会被丢弃或补占位 assistant tool_use。 |
| **Codex** | 对话存为带 parent 引用的 items（function_call + function_output 分别是 item）。截断按 (call, output) 对一起删。超 budget 时 rollback 到最近一致 checkpoint。 |
| **OpenCode** | thread 有显式 turn 标记；序列化前扫描 tool_use_id 索引，若 tool 消息的父 assistant 不在窗口内，要么扩展窗口包含父节点，要么丢弃孤儿。 |

**共同模式**：① round-aware 截断（永不在 user → assistant → tool_result 中间切）+ ② 序列化前配对校验作为安全网 + ③ 检测到孤儿时修复（扩展窗口或丢弃孤儿）。

Merak 的 `ContextOptimizer::drop_rounds()`（`libs/context/src/context_optimizer.cpp:114-177`）已实现 ①，但 `recent_history` 和 hard trim 绕过了它，且无 ②/③ 防护。

## 设计方案

采用**方案 A：分层修复**。三层防线从源头到出口依次加固，纵深防御。

### 架构

```
┌─────────────────────────────────────────────────────────┐
│ Layer 1: MemoryStore::recent_history()                  │
│   源头修复 — 窗口切在 user 边界 + 孤儿扩展/丢弃          │
└──────────────────────┬──────────────────────────────────┘
                       │ vector<Message>
                       ▼
┌─────────────────────────────────────────────────────────┐
│ Layer 2: ContextPipeline::planned_assemble()            │
│   hard trim 改为按轮次删除（复用 drop_rounds 范式）       │
└──────────────────────┬──────────────────────────────────┘
                       │ BoundContext.provider_messages
                       ▼
┌─────────────────────────────────────────────────────────┐
│ Layer 3: ContextSerializer::serialize()  [安全网]        │
│   sanitize_orphans() 预处理 — 兜底任何上游漏过的孤儿     │
└──────────────────────┬──────────────────────────────────┘
                       │ anthropic_json
                       ▼
                   Anthropic API
```

**职责边界**：
- **Layer 1** 负责"窗口选取正确"——送进 pipeline 的历史本身配对完整。
- **Layer 2** 负责"压缩后仍配对"——hard trim 不破坏轮次原子性。
- **Layer 3** 负责"序列化输出合法"——最后一道防线，检测到任何孤儿就修复，防御未来代码变更引入的新路径。

Layer 3 是纯函数、无副作用、不依赖前两层。即使前两层被误改，Layer 3 仍能保证发往 API 的 payload 合法。

---

## Layer 1：`MemoryStore::recent_history()` 修改

### 修改后逻辑

```cpp
std::vector<Message> MemoryStore::recent_history(int max_turns) const {
    std::lock_guard lock(working_memory_mutex_);
    int total = (int)working_memory_.size();
    if (total == 0) return {};

    // 1. 收集所有 user 消息索引（轮次边界）
    std::vector<int> user_indices;
    for (int i = 0; i < total; i++) {
        if (working_memory_[i].role == "user") user_indices.push_back(i);
    }

    // 2. 选取最后 max_turns 个轮次
    int keep_rounds = std::min(max_turns, (int)user_indices.size());
    if (keep_rounds <= 0) return {};
    int start = user_indices[(int)user_indices.size() - keep_rounds];

    // 3. 扩展优先：若 start 之前有连续 tool 消息（孤儿），向前扩展含父 assistant
    //    退而丢弃：若父 assistant 距离过远（> max_turns*2 + 4），改为从首个非孤儿开始
    start = adjust_for_orphan_tools(working_memory_, start, max_turns);

    std::vector<Message> result;
    for (int i = start; i < total; i++) result.push_back(working_memory_[i]);
    return result;
}
```

### `adjust_for_orphan_tools` 算法

```
输入: messages, start, max_turns
输出: adjusted_start

1. 扫描 messages[start..] 开头连续的 tool 消息
   - 记录 orphan_tool_ids = {tool_call_id of each}
2. 若无孤儿 tool 消息开头 → 返回 start
3. 向前扫描 messages[0..start-1] 找最近的 assistant 含 tool_calls
   - 若该 assistant 的 tool_calls 覆盖所有 orphan_tool_ids 且距离 ≤ max_turns*2 + 4
     → 返回该 assistant 的索引
   - 否则 → 从 start 起跳过开头连续 tool 消息，返回首个非 tool 消息索引
```

**"距离过远"阈值**：`max_turns * 2 + 4` 条消息。若父 assistant 在此距离内，扩展；否则丢弃孤儿。保证窗口最多膨胀一个轮次，不会失控。

### 边界

- `working_memory_` 为空 → 返回空。
- 没有 user 消息（全是 assistant/tool）→ 返回最后 `max_turns*2` 条，并跑 `adjust_for_orphan_tools` 兜底。
- `max_turns = 0` → 返回空。

### 接口不变

`recent_history(int max_turns) const` 签名不变。调用方 `AgentLoop` 无需改动。

---

## Layer 2：`ContextPipeline::planned_assemble()` hard trim 修改

### 当前问题

```cpp
while (opt_stats.tokens_after > model_max_tokens && msgs.size() > 2) {
    size_t target = 1;
    while (target < msgs.size() && msgs[target].role == "system") target++;
    msgs.erase(msgs.begin() + target);  // 逐条删除，可能拆散 tool_use/tool_result
}
```

### 修改后逻辑

复用 `ContextOptimizer::drop_rounds` 的轮次识别范式，但 hard trim 按 token 预算删（`drop_rounds` 按 `min_rounds_to_keep` 删），两者不能合并。每次循环重新扫描 `round_starts`，避免索引偏移陷阱。

```cpp
if (opt_stats.tokens_after > model_max_tokens) {
    auto& msgs = bound.provider_messages;
    int removed = 0;

    while (opt_stats.tokens_after > model_max_tokens) {
        std::vector<size_t> rs;
        for (size_t i = 0; i < msgs.size(); i++) {
            if (msgs[i].role == "user") rs.push_back(i);
        }
        if (rs.size() <= 1) break;  // 至少保留 1 轮

        size_t del_end = rs[1];
        int chars = 0;
        for (size_t i = rs[0]; i < del_end; i++) {
            chars += (int)msgs[i].content.size();
        }
        opt_stats.tokens_after -= chars / 3.5;
        msgs.erase(msgs.begin() + (long)rs[0], msgs.begin() + (long)del_end);
        removed += (int)(del_end - rs[0]);
    }

    stats_.hard_trims += removed;
    spdlog::warn("ContextPipeline: hard trim removed {} messages (round-aware) "
                 "(tokens_after={}, max={})", removed, opt_stats.tokens_after,
                 model_max_tokens);
}
```

### 复杂度

每次循环重新扫描 O(n)，最坏 O(n²)。hard trim 是异常路径（正常 pipeline 不触发），可接受。

### 隐含修复：`serialize()` 调用位置

原代码中 `serializer_.serialize()` 在 hard trim **之前**调用（line 72），hard trim 在 **之后**修改 `bound.provider_messages`（lines 84-101）。由于 `payload` 已经构建，hard trim 对实际发往 API 的 payload 是 **no-op** —— 只更新 `stats_.hard_trims` 和 `opt_stats.tokens_after`，不影响序列化输出。

Layer 2 修复将 `serialize()` 移到 hard trim **之后**，使 hard trim 真正影响 payload。这是修复 latent bug 的必要改动，但意味着 hard trim 首次在生产中实际生效。Layer 3 安全网作为兜底，即使 round-aware 逻辑有边界情况 bug，也会在序列化前丢弃孤儿。

### 保留行为

- 跳过 system 消息：`round_starts` 不计入 system（role != "user"）。第一轮 user 前的 system 消息不会被删。
- 至少保留 1 轮：`rs.size() <= 1` 时 break。
- 统计：`stats_.hard_trims += removed` 保留。

### 不做的事

- 不合并 `drop_rounds` 和 hard trim：触发条件不同，合并会引入耦合。
- 不抽 `round_utils.hpp`：避免跨库依赖。
- 不处理 system 消息删除：保持现状。

---

## Layer 3：`ContextSerializer::serialize()` 安全网

### 职责

在 Anthropic 序列化前，对 `payload.messages` 做配对扫描，丢弃任何孤儿。纯函数，无副作用，不依赖 Layer 1/2 的正确性。

### 两种孤儿

1. **开头孤儿 tool_result**：`tool` 消息的 `tool_call_id` 在其之前的所有 `assistant` 消息的 `tool_calls` 中找不到匹配。
2. **末尾孤儿 tool_use**：序列末尾的 `assistant` 消息含 `tool_calls`，但其后没有对应的 `tool` 消息（被截断或 loop 中途崩溃）。只处理最后一条 assistant——这是最常见的场景（loop 在 assistant 发出 tool_use 后崩溃，工具未执行）。中间或更早的孤儿 tool_use 不在安全网范围（见下方边界）。

### `sanitize_orphans()` 算法

```cpp
static std::vector<Message> sanitize_orphans(std::vector<Message> msgs) {
    // 收集所有有匹配 tool_result 的 tool_call_id
    std::set<std::string> referenced_ids;
    for (auto& m : msgs) {
        if (m.role == "tool" && m.tool_call_id) {
            referenced_ids.insert(*m.tool_call_id);
        }
    }

    std::set<std::string> produced_ids;
    for (auto& m : msgs) {
        if (m.role == "assistant") {
            for (auto& tc : m.tool_calls) produced_ids.insert(tc.id);
        }
    }

    // Pass 1: 移除开头连续的孤儿 tool 消息
    size_t i = 0;
    while (i < msgs.size() && msgs[i].role == "tool") {
        auto id = msgs[i].tool_call_id;
        if (!id.has_value() || produced_ids.count(*id) == 0) {
            spdlog::warn("ContextSerializer: dropping orphan tool_result "
                         "(tool_use_id={}) at head",
                         id.value_or("<none>"));
            msgs.erase(msgs.begin() + (long)i);
        } else {
            break;
        }
    }

    // Pass 2: 移除末尾最后一条 assistant 的孤儿 tool_use
    {
        int last_assistant = -1;
        for (int k = (int)msgs.size() - 1; k >= 0; k--) {
            if (msgs[k].role == "assistant") { last_assistant = k; break; }
        }
        if (last_assistant >= 0) {
            auto& last_a = msgs[last_assistant];
            bool all_orphan = true;
            for (auto& tc : last_a.tool_calls) {
                if (referenced_ids.count(tc.id) > 0) { all_orphan = false; break; }
            }
            if (all_orphan && !last_a.tool_calls.empty()) {
                spdlog::warn("ContextSerializer: dropping {} orphan tool_use at tail",
                             last_a.tool_calls.size());
                last_a.tool_calls.clear();
                if (last_a.content.empty()) {
                    msgs.erase(msgs.begin() + (long)last_assistant);
                }
            }
        }
    }

    return msgs;
}
```

### 接入点

在 `ContextSerializer::serialize()` 中：

```cpp
payload.messages = sanitize_orphans(std::move(ctx.provider_messages));
```

放在 `payload.messages = ctx.provider_messages;` 之后，两个格式分支之前。**对 OpenAI 和 Anthropic 格式都生效**——OpenAI API 对孤儿 tool 消息同样报错。

### 不修改 `ctx.provider_messages` 本身

`payload.messages = ctx.provider_messages;` 是值拷贝，修改 payload 不影响 `BoundContext`。

### 边界

- 空消息列表 → 直接返回。
- 无 tool 消息 → Pass 1 no-op。
- 无 assistant tool_calls → Pass 2 no-op。
- **中间孤儿（非开头非末尾）不在安全网范围**。中间孤儿意味着配对在序列中间被打乱，是 Layer 1/2 的责任。若中间出现孤儿，Anthropic API 仍会报错——这种状态只可能由未知严重 bug 产生，不应被安全网静默吞掉。

### 日志

所有丢弃操作记 `spdlog::warn`，含 `tool_use_id` 和位置（head/tail），便于线上排查。

---

## 错误处理、日志与可观测性

### 日志策略

| 层 | 触发场景 | 日志级别 | 内容 |
|---|---|---|---|
| Layer 1 | `recent_history` 扩展窗口含父 assistant | `debug` | `MemoryStore: expanded window from {} to {} to cover orphan tool_result (tool_use_id={})` |
| Layer 1 | `recent_history` 丢弃开头孤儿 tool 消息 | `warn` | `MemoryStore: dropped {} orphan tool messages at head (max_turns={})` |
| Layer 2 | hard trim 触发整轮删除 | `warn` | 已有，保留并补充 `round-aware` 标识 |
| Layer 3 | 序列化丢弃孤儿 tool_result | `warn` | `ContextSerializer: dropping orphan tool_result at head (tool_use_id={})` |
| Layer 3 | 序列化丢弃末尾孤儿 tool_use | `warn` | `ContextSerializer: dropping {} orphan tool_use at tail` |

Layer 1 扩展是 debug（信息保留，预期内），丢弃是 warn（数据丢失，需关注）。

### 错误处理边界

**不抛异常**。三层都在热路径，抛异常会中断 AgentLoop。任何异常状态用 warn 日志 + 降级处理：

- Layer 1 扩展失败 → 退化为丢弃孤儿，记 warn。
- Layer 2 hard trim 无轮次可删（只剩 1 轮仍超预算）→ break，让 payload 发出去，由 provider 返回错误（单轮超 max_tokens 是配置问题，pipeline 救不了）。
- Layer 3 `sanitize_orphans` 任何异常 → 返回原 messages 不处理，记 error。**绝不能让安全网自己崩掉**。

### 不做的事

- 不加 metrics 计数器（`PipelineStats` 无 orphan 字段，加字段污染统计语义）。
- 不加告警（hard trim 触发已是 warn，再加噪音）。
- 不修复历史数据（`working_memory_` 中可能已存不一致状态，安全网兜底即可，不回写）。

---

## 回归测试

三层测试，每层独立验证对应防线。

### 测试 1：`MemoryStore` 单测

**文件**：`libs/memory/tests/test_memory_history.cpp`（新建）  
**可执行**：`merak-memory-history-test`

构造 `MemoryConfig{.enabled = false}`，不连 DB。通过 `append_message()` 灌入历史，调 `recent_history()` 验证。

**用例**：

1. `RecentHistory_NoToolCalls_ReturnsLastNTurns` — 5 轮纯文本，`max_turns=3`，断言返回最后 6 条。
2. `RecentHistory_WithToolCalls_StartsOnUserBoundary` — 3 轮含工具调用，`max_turns=2`，断言首条是 `user`。
3. `RecentHistory_OrphanToolHead_ExpandsToParent` — naive 窗口切在 tool 消息上，断言扩展含父 assistant。
4. `RecentHistory_OrphanToolHead_TooFar_DropsOrphan` — 父 assistant 距离 > `max_turns*2 + 4`，断言丢弃开头孤儿 tool 消息。
5. `RecentHistory_EmptyMemory_ReturnsEmpty` — 空 `working_memory_`，返回空。
6. `RecentHistory_NoUserMessages_FallsBackToTail` — 只有 assistant/tool，返回最后若干条且开头无孤儿 tool。

### 测试 2：`ContextSerializer` 单测

**文件**：`libs/context/tests/test_serializer_orphans.cpp`（新建）  
**可执行**：`merak-context-serializer-test`

**用例**：

1. `Serialize_Anthropic_OrphanToolResultHead_Dropped` — 开头孤儿 tool 消息，断言 `anthropic_json["messages"][0]["role"] == "user"`，无 `tool_result` block。
2. `Serialize_Anthropic_OrphanToolUseTail_Dropped` — 末尾 assistant 含孤儿 tool_use，断言输出无 `tool_use` block（或整条移除）。
3. `Serialize_Anthropic_PairedToolUse_Preserved` — 正常配对，断言 3 条消息含完整 `tool_use` / `tool_result`。
4. `Serialize_Anthropic_MultipleOrphanToolsAtHead_AllDropped` — 开头连续 2 条孤儿 tool，断言都被丢弃。
5. `Serialize_OpenAI_OrphanToolResultHead_Dropped` — 同样输入，断言 OpenAI 格式也无孤儿。

### 测试 3：`ContextPipeline` 集成测

**文件**：`libs/context/tests/test_pipeline_hard_trim.cpp`（新建）  
**可执行**：`merak-context-pipeline-test`

**用例**：

1. `PlannedAssemble_HardTrim_KeepsRoundBoundaries` — 5 轮含工具调用，总 token 超 `model_max_tokens`，断言输出以 `user` 开头，所有 `tool` 消息的 `tool_call_id` 能在之前的 `assistant.tool_calls` 中找到匹配。
2. `PlannedAssemble_HardTrim_PreservesAtLeastOneRound` — 极小 `model_max_tokens`，断言不会清空，至少保留 1 轮。
3. `PlannedAssemble_HardTrim_DoesNotDeleteSystemMessages` — `msgs[0]` 是 system，触发 hard trim，断言 system 仍在。
4. `PlannedAssemble_EndToEnd_NoOrphanToolResult` — 长历史含工具调用，调 `planned_assemble()`，对 `anthropic_json["messages"]` 做完整配对校验。**这是 ISSUE #171 的复现测试**。

### CMake 改动

在 `tests/CMakeLists.txt` 追加：

```cmake
add_executable(merak-memory-history-test
    ${CMAKE_SOURCE_DIR}/libs/memory/tests/test_memory_history.cpp
)
target_link_libraries(merak-memory-history-test PRIVATE merak-memory)
add_test(NAME merak-memory-history-test COMMAND merak-memory-history-test)

add_executable(merak-context-serializer-test
    ${CMAKE_SOURCE_DIR}/libs/context/tests/test_serializer_orphans.cpp
)
target_link_libraries(merak-context-serializer-test PRIVATE merak-context)
add_test(NAME merak-context-serializer-test COMMAND merak-context-serializer-test)

add_executable(merak-context-pipeline-test
    ${CMAKE_SOURCE_DIR}/libs/context/tests/test_pipeline_hard_trim.cpp
)
target_link_libraries(merak-context-pipeline-test PRIVATE merak-context)
add_test(NAME merak-context-pipeline-test COMMAND merak-context-pipeline-test)
```

### 测试覆盖矩阵

| ISSUE #171 根因 | 覆盖测试 |
|---|---|
| `recent_history` 窗口切在 tool 消息上 | 测试 1 用例 2、3 |
| hard trim 拆散 tool_use/tool_result | 测试 3 用例 1、4 |
| 序列化产生孤儿 tool_result | 测试 2 用例 1、4 + 测试 3 用例 4 |
| 末尾孤儿 tool_use | 测试 2 用例 2 |

---

## 实施顺序与风险

### 实施顺序

1. **Layer 3 安全网先行**（ContextSerializer `sanitize_orphans`）  
   纯函数、无外部依赖、可立即加测试。先部署安全网，即使 Layer 1/2 还没改，也能立即阻止 400 错误线上发生。**先止血**。

2. **Layer 1 修复**（MemoryStore `recent_history`）  
   紧接着修源头，让窗口选取本身正确。

3. **Layer 2 修复**（ContextPipeline hard trim）  
   最后修，依赖对轮次边界的理解，且是异常路径。

4. **测试与 CMakeLists 同步**  
   每层改完立即加对应测试。

### 风险评估

| 风险 | 影响 | 缓解 |
|---|---|---|
| Layer 1 窗口扩展导致 token 超预算 | pipeline 后续阶段可能触发 hard trim | 扩展阈值 `max_turns*2+4` 限制膨胀；hard trim 本身也在修，能兜住 |
| Layer 2 重新扫描 O(n²) | 极长对话下 hard trim 慢 | hard trim 是异常路径；若未来成瓶颈再优化为索引偏移方案 |
| Layer 3 误删合法 tool_use（假阳性） | LLM 丢失工具调用上下文，可能重复调用 | `sanitize_orphans` 只删**确定无匹配**的孤儿，保守策略；Pass 2 只处理末尾 assistant，中间不碰 |
| 测试需要构造复杂消息序列 | 测试代码冗长 | 提取 `make_user_msg()` / `make_assistant_with_tools()` / `make_tool_result()` 辅助函数到测试文件顶部 |
| `merak-memory` 库目前没有 tests 目录 | CMake 改动需要创建目录 | 新建 `libs/memory/tests/` |

### 回滚策略

三层修改彼此独立，三个 commit 分层提交，便于回滚和 bisect：
- Layer 3：删除 `sanitize_orphans` 调用即可回滚。
- Layer 1：恢复 `max_turns * 2` 估算。
- Layer 2：恢复逐条 erase。

---

## 不在本次范围

- **社区功能**（memory 中记录的下一步规划）——本次不做。
- **`drop_rounds` 与 hard trim 逻辑统一**——方案 A 明确不抽公共 helper。
- **中间孤儿检测**——Layer 3 只处理边界，中间孤儿是未知严重 bug 的信号，不应被安全网静默吞掉。
- **`working_memory_` 持久化层修复**——ISSUE 只涉及内存中的 `recent_history`，不涉及 DB。
