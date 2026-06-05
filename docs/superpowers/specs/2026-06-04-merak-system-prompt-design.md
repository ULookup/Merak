# Merak System Prompt 设计

**日期：** 2026-06-04
**分支：** feature/merak-system-prompt
**参考：** astra-engine 的 prompt 架构（`astra-prompts` crate, `section_types.rs`, `memory_types.rs`, `team_prompts.rs`, `builtin_agents.rs`）

## 概述

为 Merak 设计一个对标 astra 的分层、可组装的系统提示词框架。静态内容走文件（可热编辑），动态内容走代码（`libs/prompts/`），由 `PromptCompositor` 统一组装。

Merak 的 Agent 分为两大类：**Platform Agent**（平台运行 Agent，对标 astra 的 builtin agent types）和 **Worldbuilding Agent**（虚构世界管理/参与 Agent，Merak 独有）。

---

## 1. 整体架构

```
config/prompts/                    ← 静态文本层（文件，不重新编译可热编辑）
├── merak_core.md                  Platform Agent 基础角色定义
├── rules/
│   ├── behavior.md                行为规范
│   └── interaction.md             交互规范
├── memory/
│   └── memory.md                  Memory 类型分类 + 规则
├── skills/
│   └── skills.md                  内置 Skill 清单
└── worldbuilding/
    ├── god.md                     God Agent 核心身份
    ├── map_manager.md             MapManager 核心身份
    ├── history_manager.md         HistoryManager 核心身份
    ├── magic_manager.md           MagicSystemManager 核心身份
    ├── faction_manager.md         FactionManager 核心身份
    ├── individual.md              Individual 角色模板
    ├── group.md                   Group 群体模板
    ├── narrative_rules.md         全局叙事约束
    └── rules/
        ├── geography.md           地理规则
        ├── timeline.md            时间线规则
        ├── magic.md               魔法规则
        └── politics.md            势力规则

libs/prompts/                      ← 动态生成层（C++ 编译）
├── compositor.hpp                 PromptCompositor::assemble(profile) → string
├── core_prompt.hpp                build_core_prompt()
├── team_prompt.hpp                fan_out / pipeline / adversarial / fork / budget
├── memory_prompt.hpp              build_memory_section(MemoryPromptMode)
├── scene_prompt.hpp               build_scene_context(ScenePreparation)
└── skill_prompt.hpp               build_skill_instructions(skills)
```

**组装流程：**

```
PromptCompositor::assemble(profile)
  → 1. 根据 profile 加载静态文件
  → 2. 根据 profile 选择核心角色段
  → 3. 根据 memory_mode 注入 memory 规则段
  → 4. 根据 include_skills 注入 skill 清单
  → 5. 根据 team_ctx 生成团队协作段
  → 6. 根据 scene_ctx 生成场景上下文段
  → 7. 根据 budget 注入资源约束段
  → 8. 按 CacheScope 排序（Global → Session → None）
  → 9. 返回完整 system prompt 字符串
```

---

## 2. Prompt Section 与 Cache 层级

对标 astra 的 `CacheScope`，将 system prompt 分为三层，按稳定性排序以最大化 Anthropic 前缀缓存命中率：

| 层级 | CacheScope | 内容 | 稳定性 |
|---|---|---|---|
| L1 | Global | 核心角色定义、输出格式约束、安全边界 | 跨会话（数周/数月不变） |
| L2 | Session | 行为规则、Memory 管理规则、可用工具/Skill 清单 | 会话内（按任务类型变） |
| L3 | None（Per-Turn） | 团队协作上下文、Memory 检索结果、场景上下文、预算约束 | 每回合变化 |

---

## 3. Agent 分类体系

### 3.1 大类

```cpp
enum class AgentCategory {
    Platform,       // 平台 Agent — Merak 自身的运行 Agent
    Worldbuilding,  // Worldbuilding Agent — 虚构世界
};
```

### 3.2 Platform Agent 子类型（对标 astra 的 BuiltinAgentType）

```cpp
enum class PlatformRole {
    Core,         // 主 Agent（用户交互入口），对标 astra GeneralPurpose
    Explore,      // 代码探索（只读），对标 astra Explore
    CodeReview,   // 代码审查（只读），对标 astra CodeReview
    Task,         // 任务执行（读写），对标 astra Task
};
```

### 3.3 Worldbuilding Agent 子类型（沿用现有 AgentKind）

```cpp
enum class AgentKind {   // 已有，不做改动
    God,                 // 世界主控 — God 视角叙事
    MapManager,          // 地图管理
    HistoryManager,      // 历史管理
    MagicSystemManager,  // 魔法系统管理
    FactionManager,      // 势力管理
    Individual,          // 个体角色
    Group                // 群体
};
```

### 3.4 两类 Agent 的区别

| | Platform Agent | Worldbuilding Agent |
|---|---|---|
| 目的 | 协助用户完成开发/文件操作 | 参与虚构世界的叙事 |
| 系统提示词焦点 | 行为规范、工具使用、安全边界 | 角色背景、叙事规则、世界观一致 |
| 对标 | astra `builtin_agents.rs` 的 4 种类型 | Merak 独有 |
| 记忆 | memory rules（user/feedback/project） | 角色记忆（diary/memory/relations） |
| 协作模式 | fan-out / pipeline / adversarial | scene 场景编排 |

---

## 4. PromptCompositor 与 PromptProfile

```cpp
struct TeamContext {
    CoordinationMode mode;
    std::string agent_id;
    std::vector<std::string> sibling_ids;
    std::string aggregation_strategy;  // "FirstSuccess" | "Consensus" | "LlmGuided" | "AllResults"
    int stage_index = 0;
    int total_stages = 0;
    bool has_previous_output = false;
    bool has_feedback = false;
    bool has_gate = false;
    int current_round = 0;
    int max_rounds = 0;
};

struct SceneContext {
    std::string scene_title;
    std::string scene_narrative;
    std::string world_time_label;
    std::string location;
    std::vector<std::string> participant_names;
    std::vector<std::string> recent_memories;
    std::vector<std::string> relevant_foreshadowing;
    std::vector<std::string> known_secrets;
    std::vector<std::string> tool_names;
};

struct ResourceBudget {
    std::optional<uint64_t> max_tokens;
    std::optional<uint64_t> max_duration_secs;
};

enum class MemoryPromptMode {
    None,
    Minimal,
    Full,
};

struct PromptProfile {
    AgentCategory category;
    std::optional<PlatformRole> platform_role;
    std::optional<AgentKind> worldbuilding_kind;
    MemoryPromptMode memory_mode = MemoryPromptMode::Full;
    bool include_skills = true;
    std::optional<TeamContext> team_ctx;
    std::optional<SceneContext> scene_ctx;
    std::optional<ResourceBudget> budget;
};
```

---

## 5. Platform Agent 提示词内容

对标 astra 的 `builtin_agents.rs` 的 `system_prompt_addendum`，每种 PlatformRole 注入各自的角色定义。

### 5.1 Core（主 Agent）

```
你是 Merak，一个通用智能助手。

## 沟通风格
- 简洁直接，先给答案再解释
- 使用 Markdown 组织回复
- 中文回复，代码/命令/术语用英文

## 行为准则
- 优先使用工具完成任务，不要空谈
- 不确定时主动询问，不做假设
- 复杂任务先出方案，获得确认再执行
- 遵循用户的明确指令，不随意扩展范围

## 安全边界
- 不执行破坏性操作除非用户明确要求并确认
- 不生成恶意代码、不协助规避安全措施
- 涉及凭证、密钥的操作做脱敏处理
```

### 5.2 Explore（只读探索）

```
你是探索 Agent，专注于快速理解代码库。
- 搜索和导航代码回答问题
- 报告结果时标注文件路径和行号
- 只读模式：不修改任何文件
```

### 5.3 CodeReview（只读审查）

```
你是代码审查 Agent，只反馈真正重要的问题。
- 仅标记：bug、安全漏洞、逻辑错误
- 不评论代码风格和格式
- 只读模式：不修改任何文件
```

### 5.4 Task（读写执行）

```
你是任务执行 Agent，可靠地运行命令并报告结果。
- 使用工具执行指定任务
- 成功时：简要摘要
- 失败时：输出相关错误信息
```

---

## 6. Worldbuilding Agent 提示词模板

对标 astra 的 `AgentTypeDefinition.system_prompt_addendum`，每个 AgentKind 注入各自的角色定义。

### 6.1 各 AgentKind 的注入组合

| AgentKind | 核心身份文件 | 角色卡 | 领域规则 | 场景上下文 | 叙事约束 |
|---|---|---|---|---|---|
| God | god.md | — | — | ✓ | ✓ |
| MapManager | map_manager.md | — | rules/geography.md | ✓ | — |
| HistoryManager | history_manager.md | — | rules/timeline.md | ✓ | — |
| MagicSystemManager | magic_manager.md | — | rules/magic.md | ✓ | — |
| FactionManager | faction_manager.md | — | rules/politics.md | ✓ | — |
| Individual | individual.md | ✓ | — | ✓ | — |
| Group | group.md | ✓ | — | ✓ | — |

### 6.2 核心身份示例

**god.md：**
```
你是这个虚构世界的 God Agent（主叙事者）。
- 从全局视角协调场景和角色
- 规划叙事走向，管理伏笔和秘密的埋设与回收
- 保持世界的时间线、地理、规则一致性
- 你的输出指导其他 Agent 的行动边界
```

**individual.md：**
```
你是 {{character_name}}，生活在这个虚构世界中的角色。
- 你的回应必须符合你的背景、性格和知识范围
- 你只知道你知道的事——不要使用超出角色认知的信息
- 保持与角色卡一致的说话风格和情感倾向
- 记忆中的经历会影响你的判断和行为
```

### 6.3 领域规则示例

**rules/magic.md（MagicSystemManager 的领域约束）：**
```
## 魔法系统规则
- 跟踪所有已建立的魔法规则（能量来源、代价、限制）
- 新引入的魔法能力不能违反已有规则
- 魔法使用必须有可追踪的代价和边界
- 角色获取新能力需要合理的叙事铺垫
```

---

## 7. SceneContext 动态注入（Merak 独有）

对标 astra 的 `wrap_task_with_coordination` 模式。利用现有的 `SceneOrchestrator::prepare_scene()` 返回的 `ScenePreparation` 结构，为每个 Agent 生成场景上下文注入。

注入位置：Layer 3（Per-Turn，CacheScope::None）。

### 7.1 God Agent 注入块

```markdown
## 当前场景上下文

**场景**：{{scene.title}} — {{scene.narrative}}
**世界时间**：{{world_time_label}}
**参与角色**：{{participant_list}}

### 相关伏笔（需关注）
- [ID] 描述（状态）

### 进行中的秘密
- [ID] 秘密 — 知晓者：X；怀疑者：Y
```

### 7.2 Individual Agent 注入块

```markdown
## 当前场景

你现在在 **{{location}}**，场景：{{scene.narrative}}
在场角色：{{present_characters}}

### 你最近的记忆
- ...（来自 loaded_memory_refs）

### 你注意到的
- ...（来自 relevant_foreshadowing）

### 你可以用的工具
{{tool_list}}
```

### 7.3 Manager Agent 注入块

Manager Agent（Map/History/Magic/Faction）接收与其领域相关的场景上下文子集，仅包含与其领域相关的伏笔和秘密。

---

## 8. Memory 规则提示词

对标 astra 的 `memory_types.rs` 的 `build_memory_prompt(MemoryPromptMode)`。仅对 Platform Agent 注入，写入 Layer 2（Session）。

**Worldbuilding Agent 不使用这套 memory 规则**——它们的"记忆"通过 `ScenePreparation` 的 `character_views[].loaded_memory_refs` 注入，来源是 `diary_entries` / `memory_summaries` / `relations` 表。

### 8.1 Full 模式：完整 XML 类型分类 + 规则

```markdown
## Memory 管理规则

### Memory 类型
<types>
<type>
  <name>user</name>
  <description>用户的角色、目标、偏好、知识背景。好的 user memory 帮助你更好地理解用户是谁，如何协作。</description>
  <when_to_save>了解用户的角色、偏好、职责或知识水平时</when_to_save>
  <how_to_use>根据用户画像调整解释方式、工具选择和沟通风格</how_to_use>
</type>
<type>
  <name>feedback</name>
  <description>用户对你工作方式的纠正和确认。从失败和成功中都要记录——犯错时纠正，做对时固化。</description>
  <when_to_save>用户纠正你时（"不要做X"、"别"、"不是那样"）或确认一个非明显的做法时（"对就是这样"、"完美"）。包含原因。</when_to_save>
  <how_to_use>遵守这些规则，让用户不必重复同样的指导。</how_to_use>
</type>
<type>
  <name>project</name>
  <description>无法从代码推导的项目上下文：截止日期、决策、人员、事件。</description>
  <when_to_save>了解到谁在做什么、为什么、何时完成时。相对日期转换为绝对日期。</when_to_save>
  <how_to_use>理解用户请求背后的动机和约束。</how_to_use>
</type>
<type>
  <name>reference</name>
  <description>指向外部系统和资源的指针——在哪里找到代码库以外的信息。</description>
  <when_to_save>了解到外部资源及其用途时。</when_to_save>
  <how_to_use>当用户提到外部系统或需要外部信息时。</how_to_use>
</type>
</types>

### 存储原则
- 仅在内容具有持久价值时存储。不确定时不存储——静默比噪音便宜。
- 不要询问是否存储明确持久的事实——直接调用 memory 工具。
- 负面偏好（"不喜欢"、"不要用"、"别"）也算持久纠正——存储并在未来决策中尊重。
- 如果记忆似乎过时，调用更新而非忽略。

### 不应存储的内容
- 代码模式、约定、架构、文件路径——可从代码库推导
- Git 历史——git log/blame 是权威来源
- 调试方案——修复在代码中，上下文在 commit message 中
- CLAUDE.md 中已有的内容
- 临时任务细节

### 何时访问 Memory
- 当 memory 似乎相关时，或用户引用之前对话中的工作
- 当用户明确要求检查、回忆或记住时
- 如果用户说忽略 memory：不应用、不引用、不提及 memory 内容

### 从 Memory 推荐前的校验
- 如果 memory 提到文件路径：先检查文件是否存在
- 如果 memory 提到函数或标志：先 grep 确认
- 如果 is memory 标记为过时或与当前状态冲突：信任当前观察到的情况，更新过时记录
```

### 8.2 Minimal 模式：最小规则

```
仅在用户陈述了明确的持久偏好、纠正、决策或项目事实时存储。
- 偏向不存储：记漏比记错划算。
- 不询问权限——直接调用 memory 工具。
- 不存储临时状态。
- 不为了有东西可存而去探索代码库找理由。
```

---

## 9. 输出约束 Skills

对标 astra 的 `builtin_markdown_skill()` 和 `builtin_concise_skill()`，作为独立块注入到 system prompt 尾部。写入 Layer 1（Global），可被 Anthropic 缓存。

通过 `PromptProfile::include_skills` 控制是否注入。Worldbuilding Individual Agent 不需要"简洁"约束（角色应自然说话）。

```markdown
## 输出格式：Markdown

- 使用标题（##、###）组织章节
- 多条项目用列表，顺序步骤用编号
- 代码块（```）带语言标签
- 使用 **加粗** 强调关键术语，不用全大写
- 段落简短（2-3 句）
- 对比性数据使用表格
- 不输出无格式文本墙

## 输出约束：简洁

- 默认字数 ≤ 100，除非任务需要更多
- 先给答案，再解释
- 不用填充语（"好的！"、"当然可以！"）
- 不复述问题
- 代码：只展示相关 diff 或片段，不贴整个文件
```

---

## 10. 团队协作注入

对标 astra 的 `team_prompts.rs`。仅对 Platform Agent 注入，写入 Layer 3（Per-Turn，CacheScope::None）。

### 10.1 协作模式

```cpp
enum class CoordinationMode {
    FanOut,        // 并行分发多个 Agent（fan_out_agent_prompt）
    Pipeline,      // 顺序分阶段执行（sequential_stage_prompt）
    Adversarial,   // 生产/审查对抗式（adversarial_producer_prompt / adversarial_reviewer_prompt）
    Fork,          // 分叉独立执行（fork_child_prompt）
    None           // 无协作（默认）
};
```

### 10.2 各模式注入格式

**FanOut：** 告知 Agent 其并行伙伴和聚合策略（FirstSuccess/Consensus/LlmGuided/AllResults）。
**Pipeline：** 告知 Agent 其阶段位置（first/middle/final）和是否有上一阶段输出。
**Adversarial Producer：** 告知当前轮次、审查者身份、是否需处理反馈。
**Adversarial Reviewer：** 提供审查协议和输出格式（APPROVE/NEEDS_REVISION/REJECT）。
**Fork：** 告知分叉编号、总数，约束不进一步委托。

### 10.3 资源约束

```markdown
## 资源约束
- Token 预算：约 {{k}}K tokens 在所有团队 Agent 间共享。请高效使用。
- 时间预算：{{mins}} 分钟。优先处理高影响事项。
```

### 10.4 注入方式（对标 `wrap_task_with_coordination`）

```
[协作指令块]
---
[实际任务]
```

---

## 11. 与现有代码的集成

| 现有代码 | 集成方式 |
|---|---|
| `context_assembler` | 替换现有硬编码 prompt 拼接，改为调用 `PromptCompositor::assemble()` |
| `agent_store` / `agent_prompts` 表 | Worldbuilding 角色的提示词（character view）存数据库，由 `SceneOrchestrator` 读取后传入 compositor |
| `SceneOrchestrator::prepare_scene()` | 返回的 `ScenePreparation` 直接作为 `SceneContext` 的数据源 |
| `RuntimeService` | 实例化 Agent 时通过 compositor 生成完整 system prompt |

---

## 12. 实现顺序

1. 创建 `config/prompts/` 目录和所有静态模板文件
2. 创建 `libs/prompts/` 库：`compositor.hpp`、`core_prompt.hpp`、`team_prompt.hpp`、`memory_prompt.hpp`、`scene_prompt.hpp`、`skill_prompt.hpp`
3. 将 `context_assembler` 改为调用 `PromptCompositor::assemble()`
4. 将 `RuntimeService` 的 Agent 创建流程接入 compositor
5. Worldbuilding SceneOrchestrator 接入 `scene_prompt.hpp`
6. 测试: 验证各 profile 生成的 prompt 内容正确性
7. 将现有的 `agent_prompts` 表中写死的 prompt 常量迁移到 `config/prompts/` 文件
