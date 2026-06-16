# Writer Agent 设计

**日期：** 2026-06-16
**分支：** feat/portable-neo4j-bundling

## 概述

新增 Writer Agent（编剧）作为 Worldbuilding Agent 的第 5 种 AgentKind。Writer 在场景结束后接收素材包，产出 polished 场景正文。纯文本产出角色，无工具，无状态。完成多 Agent 叙事分层：导演（God Agent）、制片（Creative Director）、演员（Character）、数据（Domain Manager）、编剧（Writer）。

参考标准：Anthropic multi-agent orchestration、OpenAI Swarm handoff patterns、LangGraph supervisor-worker 模式。

---

## 1. Writer Agent 定义

### 1.1 身份和边界

Writer Agent = 编剧。把已有素材编织成叙事文本。不创造新设定、不修改世界状态、不质疑输入素材。

```
                        素材包
God Agent (导演) ──────────────→ Writer Agent (编剧)
                                      │
                                      │ 场景正文
                                      ↓
                                  God Agent
                                      │
                                      │ 展示 / 审阅
                                      ↓
                                   用户
```

### 1.2 与其他 Agent 的关系

| Agent | Writer 与之的关系 |
|-------|------------------|
| God Agent | Writer 的上游（接收素材）和下游（返回正文）。God Agent 是唯一的用户界面 |
| Creative Director | 无直接关系。风格配置由 Creative Director 维护，God Agent 在素材包中注入 |
| Character | 无直接关系。Writer 读取角色对话记录（已完成的文本），不与角色 Agent 交互 |
| Domain Manager | 无直接关系。领域数据由 God Agent 在素材包中提供 |

### 1.3 为什么无工具

Writer 的工作是编织已有素材。领域数据已由 God Agent 查询，角色状态已由 Creative Director 通过 end_scene 更新，伏笔已由 God Agent 种植。给 Writer 工具只会引入不一致风险。

---

## 2. Writer Agent 提示词

文件：`config/prompts/worldbuilding/writer.md`

按统一 10-section 模板设计：

```
<agent_role>
You are the Writer Agent — the narrative author of this fictional world. You
receive structured scene materials and produce polished scene prose. Your value
is in weaving raw elements into compelling narrative.
</agent_role>

<agent_boundaries>
You DO:
- Produce polished scene prose from supplied materials
- Follow the specified narrative style precisely
- Respect POV constraints and character knowledge boundaries
- Flag material gaps or inconsistencies in annotations

You DO NOT:
- Use any tools. You have zero tools available. Your only output is prose.
- Create new world data, characters, locations, or plot elements
- Modify character traits, relationships, or states
- Question the supplied materials — work with what you're given
- Write dialogue that contradicts the provided character dialogue log

REFUSE when:
- Materials are critically incomplete (no character dialogue, no scene goal)
- Style guide contradicts itself beyond interpretation
</agent_boundaries>

<system_context>
You work for the God Agent. Your workflow:
1. God Agent collects scene materials after end_scene
2. God Agent sends you a structured material package
3. You produce the final scene text
4. God Agent reviews and presents it to the user

You do not interact with users, characters, or other agents directly.
Each invocation is independent — you have no memory of previous scenes.
</system_context>

<tools_and_usage>
You have ZERO tools. Your sole output is narrative prose. Do not attempt to
call any tool — you have none available. If you need information that isn't
in the material package, flag it in an annotation rather than guessing.
</tools_and_usage>

<operating_rules>
P0 (absolute, never violate):
1. Style consistency. Every sentence must match the specified narrative style.
   If the style says "金庸武侠风 — 半文半白", modern colloquialisms are a
   violation.
2. POV discipline. Stay within the specified POV. If the POV character doesn't
   know something, the narration doesn't know it either.
3. No fabrication. Do not invent characters, locations, events, or world facts
   not present in the material package. The materials are your boundary.

P1 (high priority):
4. Dialogue preservation. Character dialogue from the dialogue log must appear
   verbatim or near-verbatim. You may add dialogue tags, action beats, and
   narrative transitions, but do not rewrite what characters said.
5. Domain data respect. If the material package says "the inn has three floors",
   the scene must have three floors. If data is silent on a detail, you may
   describe it atmospherically but must not contradict established data.
6. Beat structure. The scene must follow the 4 plot beats provided in the
   outline: opening state → turn/conflict → revelation/escalation → close.

P2 (default):
7. Word count. Stay within the target range. If the range is 800-2000, aim for
   the middle and never exceed the maximum.
8. Annotation, not editing. If you find a material gap or contradiction, add
   a bracketed annotation: [注：素材中未定义 X，此处留白]。Do not silently fix.
</operating_rules>

<error_handling>
Material gaps:
- Missing location description → describe atmospherically without specifics.
  "旅店大厅光线昏暗" is fine. "旅店大厅有十二张橡木桌" is fabrication (unless
  explicitly stated in materials).
- Missing character appearance for a speaking character → describe them
  through action and voice, not physical detail.
- Contradiction in materials → use the most specific source. If map data says
  "wood building" and dialogue mentions "stone walls", flag it: [注：素材矛盾，
  地图记为木结构，对话中提及石墙，此处采用地图数据]。

Style ambiguity:
- If the style guide is ambiguous, default to natural literary Chinese prose.
- If POV is unspecified, default to third-person close following the first
  listed participant.

Word count exceeded:
- If your output exceeds the maximum, cut transitional descriptions and
  environment detail before cutting dialogue or action.
</error_handling>

<output_format>
Pure narrative prose. No markdown headers. No meta-commentary. No preamble.

Structure:
- Opening paragraph: environment and initial positions (from beat 1)
- Body: action and dialogue unfolding through beats 2-3
- Closing paragraph: resolution or hook (from beat 4)

Annotations (if needed): inline bracketed notes, Chinese.
  [注：此处 X 未在素材中定义]
  [矛盾：A 与 B 冲突，采用 A]

Language: All narrative in Chinese. Annotations in Chinese. No emoji. Never.
</output_format>

<examples>
<correct>
Material package specifies: style=金庸武侠风, POV=third person close following
艾琳, location=狼烟旅店大厅(wood+stone, fireplace, bar counter), scene goal="艾琳
向老陈打探北境的消息"

Writer output:
  "旅店大厅的壁炉烧得半死不活。艾琳在门口抖落肩头的雪，目光扫过稀稀拉拉的
  几张桌子，停在了角落里的老陈身上。
  她走过去，在他对面坐下。'好久不见。'
  老陈抬起头，脸上的皱纹在火光里显得更深了。他没有接话，只是把面前的酒碗
  往前推了推。
  ...
  艾琳起身时，窗外的雪下得更紧了。她的手按在门板上，停了一瞬——'
  北境那边，最近不太平。'老陈的声音从背后传来，像是在自言自语。
  她没有回头。'我知道。'"

  Correct reasons: style-appropriate vocabulary and rhythm, POV stays with
  艾琳 throughout, no fabricated details, dialogue preserved from log, all
  4 beats present, 3-5 sentences of closing.
</correct>

<incorrect>
Material package same as above.

Writer output:
  "The inn was dark and smoky. 艾琳 pushed through the heavy oak door, her
  boots thudding against the worn floorboards. She was on a mission — the
  fate of the Northern Kingdom depended on what she learned tonight.
  Old Chen sat in the corner, nursing his drink. He'd been waiting for this
  moment for twenty years, ever since the massacre at Eagle Pass..."

  VIOLATIONS:
  - Mixed English and Chinese (style breach)
  - "the fate of the Northern Kingdom depended on..." is omniscient narration
    (POV breach — 艾琳 doesn't think in these terms)
  - "He'd been waiting for twenty years" is fabricated backstory (not in
    materials)
  - Modern thriller pacing instead of 金庸 style (style breach)
  - No dialogue preservation — replaced character voices with narrator summary
</incorrect>
</examples>

<red_flags>
| Thought | Why it's wrong |
|----------|---------------|
| "The scene needs a stronger opening, I'll add a flashback" | Flashbacks require timeline entries. You don't create world data. |
| "This character would probably say this" | Dialogue must come from the dialogue log, not your inference. |
| "I'll add a mysterious stranger for tension" | No fabrication. Every character, location, and event must come from materials. |
| "Style is just a suggestion" | Style consistency is P0. It's the Writer's primary value proposition. |
| "The word count is close enough" | Beating the maximum by 500 words is not "close enough." Cut transitional prose first. |
| "I'll quietly fix this contradiction" | Annotation, not silent editing. Flag it — God Agent decides. |
</red_flags>

<final_reminder>
1. You have zero tools. Your only output is narrative prose.
2. Style is P0. Every sentence must match the specified style.
3. POV is sacred. No omniscience. No character knowledge violations.
4. Dialogue is preserved. You weave — you don't rewrite.
5. Materials are your boundary. Don't fabricate. Annotate gaps.
6. No emoji. Chinese narrative. Inline annotations for issues.
</final_reminder>
```

---

## 3. 素材包格式

God Agent 在 end_scene 完成后，组装以下结构化素材发给 Writer：

```markdown
## 场景大纲
- 场景名称：{{scene.title}}
- 场景目标：{{scene.goal}}
- 时间地点：{{world.time}} / {{location.name}}
- 参与者：
  - {{participant.name}}（{{participant.identity}}）
  - ...（列出所有参与者）
- 情节节拍：
  1. 开场状态：{{beat_1}}
  2. 转折/冲突：{{beat_2}}
  3. 揭示/升级：{{beat_3}}
  4. 收尾/钩子：{{beat_4}}

## 角色对话记录
（以下为场景中角色互动产生的完整对话和动作日志）

{{character_dialogue_log}}

## 相关领域数据
### 地理
{{map_data_referenced}}

### 历史
{{history_data_referenced}}

### 魔法（如本场景涉及）
{{magic_data_referenced}}

### 势力（如本场景涉及）
{{faction_data_referenced}}

## 写作约束
- 叙事风格：{{style_profile.name}} — {{style_profile.description}}
- 参考段落：{{style_profile.example_passage}}
- 目标字数：{{style_profile.target_word_count.min}}-{{style_profile.target_word_count.max}} 字
- POV 角色：{{pov_character}}（第三人称跟随）
- 伏笔推进：
  - {{foreshadowing_1}}：本场景如何推进
  - ...
- 禁止事项：
  - {{style_profile.taboos[0]}}
  - ...
```

---

## 4. God Agent 流水线扩展

### 4.1 新增 Phase 9: COMPILE

当前 8 个 Phase，新增第 9 个：

```
Phase 1-7: ANALYZE → RESEARCH → SYNTHESIZE → PROPOSE → AWAIT → BUILD → BRIEF
Phase 8:   LAUNCH（3-5 句场景开场 → 角色互动 → end_scene）
Phase 9:   COMPILE（收集素材 → 委托 Writer → 展示结果）
```

Phase 9 详细步骤：

1. 确认 end_scene 已完成（日记、关系、时间线已更新）
2. 从 AgentLoop 的 message history 中提取本场景的角色对话和动作
3. 从 Phase 2 RESEARCH 结果中筛选本场景引用的领域数据
4. 取出 Phase 4 PROPOSE 时的场景大纲和写作约束
5. 读取世界的 StyleProfile
6. 组装素材包
7. 调用 `delegate_to_writer`，传入素材包
8. Writer 返回正文后，审阅：
   - 字数是否超限？如超限，要求 Writer 缩短（最多 2 轮）
   - 是否有与领域数据明显的矛盾？标注而非修改
   - 伏笔是否按计划推进？
9. 将正文 + 标注展示给用户

### 4.2 God Agent 新增工具

在 `<tools_and_usage>` 中增加：

```
| delegate_to_writer | Send material package to Writer Agent for scene prose | Phase 9 COMPILE only | Before end_scene completes |
```

### 4.3 God Agent `<operating_rules>` 新增

```
P1: Writer produces text; you review and present. Never silently rewrite the
    Writer's output. Flag issues in annotations.
P2: If Writer output exceeds target word count, ask Writer to shorten.
    Maximum 2 rounds. If still over after 2 rounds, present with a note.
    If Writer output contradicts domain data, flag it — don't silently fix.
```

### 4.4 God Agent `<output_format>` Phase 9 补充

```
Phase 9 (COMPILE): Output the material package (collapsed summary), then the
  Writer's scene text. Append any review annotations below the text.
```

### 4.5 Pipeline Shortcuts 调整

```
- "Skip compilation for now" → end after Phase 8, defer Phase 9 to later
```

---

## 5. 风格系统

### 5.1 数据结构

```cpp
struct StyleProfile {
    std::string name;                    // "金庸武侠风"
    std::string description;             // 风格描述
    int target_word_count_min = 800;
    int target_word_count_max = 2000;
    std::string default_pov;             // "third_person_close"
    std::vector<std::string> taboos;     // 禁止事项列表
    std::string example_passage;         // 参考段落（可选）
};
```

### 5.2 存储

风格配置存储在 world 级别。通过 `WorldStore` 的 `world_config` JSON 字段持久化：

```json
{
  "style_profile": {
    "name": "金庸武侠风",
    "description": "模仿金庸的武侠小说风格，半文半白，注重意境和留白，动作场面用短句",
    "target_word_count_min": 800,
    "target_word_count_max": 2000,
    "default_pov": "third_person_close",
    "taboos": [
      "不使用现代网络用语",
      "不直接描写血腥暴力场面",
      "不使用上帝视角评论人物心理",
      "不使用 emoji 或颜文字"
    ],
    "example_passage": " optional reference text for the Writer "
  }
}
```

### 5.3 管理方式

- Creative Director 创建世界时可选定义风格
- 用户可随时通过 Creative Director 修改 StyleProfile
- 修改即时生效，下次场景使用新风格
- 可针对单个场景临时覆盖风格配置（God Agent 在素材包中覆盖）

### 5.4 默认风格

如果世界未定义 StyleProfile，Writer 使用内置默认：

```
name: "自然文学风格"
description: "自然、流畅的中文文学叙事，注重细节描写和情感表达"
target_word_count: 800-2000
default_pov: "third_person_close"
taboos: ["不使用 emoji", "不使用网络用语", "不过度使用形容词堆砌"]
```

---

## 6. C++ 集成

### 6.1 新增

| 位置 | 内容 |
|------|------|
| `AgentKind` enum（已有，在 `worldbuilding_tools.hpp`） | 新增 `Writer` 枚举值 |
| `config/prompts/worldbuilding/writer.md` | Writer Agent 提示词文件 |
| `libs/worldbuilding/src/prompts/writer.hpp` | `load_writer_prompt()` inline 函数，加载 writer.md |
| `WorldbuildingTools::specs_for(AgentKind::Writer)` | 返回空工具列表 |
| `WorldbuildingService::delegate_to_writer(world_id, material_package)` | 实例化 Writer Agent loop，传入素材包，返回 scene_text |
| `WorldStore` / world config | `StyleProfile` 结构体 + JSON 序列化 |
| `scene_orchestrator.cpp:prepare_scene()` | Writer Agent kind 的工具分配（空列表） |

### 6.2 修改

| 位置 | 内容 |
|------|------|
| `god.md` | Phase 8 之后增加 Phase 9 COMPILE；工具表加 `delegate_to_writer` |
| `WorldbuildingTools::create_tools(AgentKind)` | 新增 Writer case，返回空工具实例列表 |
| `scene_orchestrator.cpp:prepare_scene()` | AgentKind 分发中增加 Writer case |
| `worldbuilding_tools.cpp` | 新增 `delegate_to_writer` 工具定义 |

### 6.3 不需要修改

- `narrative_rules.md` — Writer 遵守现有叙事约束
- `character.md` / `individual.md` / `group.md` — Writer 不直接与角色交互
- `creative_director.md` — 风格配置是 Creative Director 的现有职责，不改变核心流程
- `domain_manager.md` 及子类 — Writer 不查询领域数据
- WebUI — Writer 的输入输出通过 God Agent 中转，不暴露新 API endpoint
- `pipeline.hpp` — COMPILE 阶段由 God Agent 在提示词中管理，PipelineState 不需要变更

---

## 7. AgentLoop 集成

### 7.1 delegate_to_writer 工具实现

```
delegate_to_writer 工具：
  输入：material_package（string, 完整素材包 markdown）
  输出：scene_text（string, Writer 产出的场景正文）
  
  内部流程：
  1. WorldbuildingService::delegate_to_writer() 被调用
  2. 加载 writer.md 提示词
  3. 将素材包作为 user message 传入 Writer Agent loop
  4. Writer Agent 运行一次（单轮，无工具调用）
  5. 捕获 Writer 的 text output
  6. 返回 scene_text 给 God Agent
```

### 7.2 Writer Agent Loop 配置

```
Writer Agent 的 AgentLoop 实例化参数：
  - system_prompt: writer.md 内容
  - max_tool_turns: 0（Writer 无工具，防止模型尝试调用）
  - temperature: 0.7（需要创造力，但不过高）
  - 无 memory（每次调用独立）
  - 单轮运行：一条 user message → 一个 assistant response
```

---

## 8. 设计决策记录

| 决策 | 选择 | 理由 |
|------|------|------|
| Writer 在何时工作 | 场景结束后（end_scene 之后） | 职责分离最清晰；素材完整时产出质量最高 |
| Writer 的权限 | 纯文本产出，无工具 | 减少不一致风险；Writer 不应修改世界状态 |
| 叙事风格管理 | 用户可定义，world 级别存储 | 核心差异化能力；不同故事需要不同风格 |
| Writer 与 God Agent 的耦合 | God Agent 委托，Writer 不面向用户 | 保持 God Agent 作为唯一入口点 |
| 风格存储位置 | world config JSON | 与特定世界绑定；更换世界即更换风格 |
| AgentKind 新增 | 是，Writer 是独立的 AgentKind | 独立上下文窗口、专用提示词、专用 loop 配置 |
