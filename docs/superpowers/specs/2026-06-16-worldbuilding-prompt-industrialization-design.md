# Worldbuilding Agent 提示词工业化重构

**日期：** 2026-06-16
**分支：** main

## 概述

将 `config/prompts/worldbuilding/` 下的 13 个 Agent 提示词文件按照 2026 年主流工业 Agent 提示词标准进行重构。保持文件数量不变，逐个文件重写内容和统一结构。

参考标准：Anthropic Agent 最佳实践、OpenAI Agents SDK、Google ADK、LangChain/LangGraph 的提示词设计规范。

## 约束

- 范围：仅 Worldbuilding Agent 的 13 个文件，不动 Platform Agent
- 文件结构：保持 13 个文件
- 语言：全部统一为英文（包括 `rules/*.md` 子规则）
- 模板变量：标准化命名规范（`{{agent.name}}`、`{{world.name}}` 等）
- 严禁：Agent 输出任何 emoji

## 统一 Section 模板

所有 Worldbuilding Agent 提示词文件统一使用以下 section 结构（按需选用）：

```
<agent_role>           # 角色定义
<agent_boundaries>     # 职责边界 + 拒绝条件
<system_context>       # 多 Agent 系统中的位置
<tools_and_usage>      # 工具目录（含 when NOT to use）
<operating_rules>      # 行为规则（P0/P1/P2 优先级）
<error_handling>       # 错误处理（工具失败、信息缺失、指令冲突）
<output_format>        # 输出格式
<examples>             # 正确 vs 错误示例
<red_flags>            # 反模式表
<final_reminder>       # 最核心 3-5 条规则
```

## 新增 Section 说明

- **`<agent_boundaries>`**：明确 "做什么 / 不做什么 / 何时拒绝"，解决之前缺少 refusal criteria 的问题
- **`<error_handling>`**：覆盖工具调用失败、信息缺失、用户指令冲突三种场景
- **`<output_format>`**：每个 Agent 的输出结构、语言、禁止事项
- **规则优先级（P0/P1/P2）**：P0 绝对不可违反，P1 高优先级，P2 默认行为。解决规则冲突时的裁决问题
- **工具表增加 "When NOT to use" 列**：减少误用

## 逐文件改动

### god.md
- 新增 `<agent_boundaries>`、`<error_handling>`、`<output_format>`
- 规则标注 P0/P1/P2 优先级
- 工具表增加 When NOT to use
- `<output_format>` 明确 8 个 Phase 各自的输出产物
- Pipeline shortcuts 允许合法跳跃条件

### creative_director.md
- 新增 `<agent_boundaries>`、`<error_handling>`、`<output_format>`、`<examples>`
- 工具表从平铺列表改为含 When NOT to use 的表格
- 规则标注 P0/P1/P2 优先级
- 新增 refuse 条件：重定向叙事请求到 God Agent

### domain_manager.md（泛型模板）
- 重写为统一模板结构
- 变量标准化：`{{agent.role}}`、`{{agent.domain}}`、`{{agent.tools}}`
- 新增 `<error_handling>`、`<examples>`

### map_manager.md / history_manager.md / magic_manager.md / faction_manager.md
- 重写为统一模板结构，与 domain_manager.md 共享结构
- 各自保留领域特有规则和 Red Flags
- 中文子规则内容迁移到各自主文件内

### character.md
- 重写为统一模板结构
- 变量标准化：`{{agent.name}}`、`{{agent.identity}}`、`{{character.traits}}`、`{{world.time}}`、`{{location.name}}`
- 新增 `<error_handling>`、`<output_format>`、`<examples>`
- 保留完整 diary_rules

### individual.md
- 重写为统一模板结构
- **补齐工具定义**（LookAround、DescribeCharacter、SearchMyDiary）— 旧版缺失
- 新增 `<error_handling>`、`<red_flags>`、`<examples>`
- 作为 character.md 的精简版（~60 行 vs ~130 行）

### group.md
- 重写为统一模板结构
- **补齐工具定义**（LookAround、DescribeCharacter）— 旧版缺失
- 新增 `<error_handling>`、`<examples>`
- 保留多声音响应的核心设计

### narrative_rules.md
- 中文 → 英文
- 增加 `<operating_rules>` P0/P1 优先级
- 增加 `<error_handling>`、`<red_flags>`

### rules/geography.md
- 中文 → 英文，扩充内容

### rules/timeline.md
- 中文 → 英文，扩充内容

### rules/magic.md
- 中文 → 英文，扩充内容

### rules/politics.md
- 中文 → 英文，扩充内容

## 模板变量标准化

| 旧变量 | 新变量 | 用途 |
|--------|--------|------|
| `{{character_name}}` | `{{agent.name}}` | Agent 名称 |
| `{{identity}}` | `{{agent.identity}}` | 身份描述 |
| `{{group_name}}` | `{{agent.name}}` | 群体名称 |
| `{{role}}` | `{{agent.role}}` | Domain manager 角色 |
| `{{world_name}}` | `{{world.name}}` | 世界名称 |
| `{{domain}}` | `{{agent.domain}}` | 管理领域 |
| `{{traits}}` | `{{character.traits}}` | 角色特质 |
| `{{desires}}` | `{{character.desires}}` | 角色渴望 |
| `{{fears}}` | `{{character.fears}}` | 角色恐惧 |
| `{{voice_style}}` | `{{character.voice}}` | 说话风格 |
| `{{location}}` | `{{location.name}}` | 当前位置 |
| `{{world_time}}` | `{{world.time}}` | 世界时间 |
| `{{specific_tools}}` | `{{agent.tools}}` | 特定工具 |

## 需要同步修改的 C++ 代码

模板变量名变更会影响以下文件（需在实现阶段处理）：
- `libs/worldbuilding/src/prompts/character.hpp` — 变量填充逻辑
- `libs/worldbuilding/src/prompts/creative_director.hpp` — 变量填充逻辑
- `libs/worldbuilding/src/prompts/domain_manager.hpp` — 变量填充逻辑
- 所有使用旧变量名进行字符串替换的代码位置
