# Writer Agent Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Writer Agent (编剧) as the 5th AgentKind — pure text output, no tools, receives material packages from God Agent after end_scene and produces polished scene prose.

**Architecture:** Add `Writer` to the `AgentKind` enum, register it in all switch/if-else dispatch chains, create the writer prompt and loader, and extend god.md with Phase 9 COMPILE + `delegate_to_writer` tool. Writer has zero tools and is invoked single-turn by God Agent via `WorldbuildingService::delegate_to_writer()`.

**Tech Stack:** C++17, Markdown prompts, nlohmann/json

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `libs/worldbuilding/include/merak/worldbuilding/world_models.hpp` | Modify | `AgentKind` enum + `to_string` |
| `libs/worldbuilding/src/agent_store.cpp` | Modify | `agent_kind_from_string` parser |
| `libs/worldbuilding/src/world_store.cpp` | Modify | `agent_kind_from_string` parser (duplicate) |
| `libs/worldbuilding/src/worldbuilding_tools.cpp` | Modify | `create_tools` switch — empty tools for Writer |
| `config/prompts/worldbuilding/writer.md` | Create | Writer Agent system prompt (~220 lines, 10-section template) |
| `libs/worldbuilding/src/prompts/writer.hpp` | Create | `load_writer_prompt()` inline loader |
| `config/prompts/worldbuilding/god.md` | Modify | Phase 9 COMPILE, delegate_to_writer tool, P1 rules, output format, pipeline shortcuts |

---

### Task 1: Add Writer to AgentKind enum and to_string

**Files:**
- Modify: `libs/worldbuilding/include/merak/worldbuilding/world_models.hpp:12-21`
- Modify: `libs/worldbuilding/include/merak/worldbuilding/world_models.hpp:274-294`

**Context:** The `AgentKind` enum currently has 8 values (God, MapManager, HistoryManager, MagicSystemManager, FactionManager, RelationManager, Individual, Group). Writer is added between Group and the closing brace. The `to_string` function mirrors the enum.

- [ ] **Step 1: Add `Writer` to AgentKind enum**

In `libs/worldbuilding/include/merak/worldbuilding/world_models.hpp`, change lines 19-21 from:
```cpp
    Individual,
    Group
};
```
to:
```cpp
    Individual,
    Group,
    Writer
};
```

- [ ] **Step 2: Add Writer case to to_string(AgentKind)**

In `libs/worldbuilding/include/merak/worldbuilding/world_models.hpp`, change lines 290-293 from:
```cpp
    case AgentKind::Group:
        return "group";
    }
    return "individual";
```
to:
```cpp
    case AgentKind::Group:
        return "group";
    case AgentKind::Writer:
        return "writer";
    }
    return "individual";
```

- [ ] **Step 3: Verify compilation**

Run: `cd /home/icepop/Merak && cmake --build build --target merak_worldbuilding 2>&1 | tail -20`
Expected: Successful compilation (may get warnings about unhandled switch cases in other files — those are fixed in Tasks 2-3).

- [ ] **Step 4: Commit**

```bash
cd /home/icepop/Merak
git add libs/worldbuilding/include/merak/worldbuilding/world_models.hpp
git commit -m "feat: add Writer to AgentKind enum and to_string"
```

---

### Task 2: Add writer to agent_kind_from_string parsers

**Files:**
- Modify: `libs/worldbuilding/src/agent_store.cpp:24-50`
- Modify: `libs/worldbuilding/src/world_store.cpp:24-34`

**Context:** Both `agent_store.cpp` and `world_store.cpp` have independent copies of `agent_kind_from_string`. Both need a `"writer"` case mapping to `AgentKind::Writer`.

- [ ] **Step 1: Add writer case in agent_store.cpp**

In `libs/worldbuilding/src/agent_store.cpp`, change lines 46-49 from:
```cpp
    if (value == "group") {
        return AgentKind::Group;
    }
    throw std::runtime_error("unknown agent kind: " + value);
```
to:
```cpp
    if (value == "group") {
        return AgentKind::Group;
    }
    if (value == "writer") {
        return AgentKind::Writer;
    }
    throw std::runtime_error("unknown agent kind: " + value);
```

- [ ] **Step 2: Add writer case in world_store.cpp**

In `libs/worldbuilding/src/world_store.cpp`, change lines 31-33 from:
```cpp
    if (value == "group") return AgentKind::Group;
    if (value == "individual") return AgentKind::Individual;
    throw std::runtime_error("unknown agent kind: " + value);
```
to:
```cpp
    if (value == "group") return AgentKind::Group;
    if (value == "individual") return AgentKind::Individual;
    if (value == "writer") return AgentKind::Writer;
    throw std::runtime_error("unknown agent kind: " + value);
```

- [ ] **Step 3: Verify compilation**

Run: `cd /home/icepop/Merak && cmake --build build --target merak_worldbuilding 2>&1 | tail -20`
Expected: Successful compilation.

- [ ] **Step 4: Commit**

```bash
cd /home/icepop/Merak
git add libs/worldbuilding/src/agent_store.cpp libs/worldbuilding/src/world_store.cpp
git commit -m "feat: add writer to agent_kind_from_string parsers"
```

---

### Task 3: Add Writer case to WorldbuildingTools::create_tools

**Files:**
- Modify: `libs/worldbuilding/src/worldbuilding_tools.cpp:3035-3037`

**Context:** The `create_tools` switch currently has cases for God, Individual, MapManager, HistoryManager, MagicSystemManager, FactionManager, RelationManager, and Group. Writer returns an empty tool list (same as Group).

- [ ] **Step 1: Add Writer case returning empty tools**

In `libs/worldbuilding/src/worldbuilding_tools.cpp`, change lines 3035-3037 from:
```cpp
    case AgentKind::Group:
        break;
    }
```
to:
```cpp
    case AgentKind::Group:
        break;
    case AgentKind::Writer:
        break;
    }
```

- [ ] **Step 2: Verify compilation**

Run: `cd /home/icepop/Merak && cmake --build build --target merak_worldbuilding 2>&1 | tail -20`
Expected: Successful compilation with no warnings about unhandled AgentKind cases.

- [ ] **Step 3: Commit**

```bash
cd /home/icepop/Merak
git add libs/worldbuilding/src/worldbuilding_tools.cpp
git commit -m "feat: add Writer case to create_tools (empty tool list)"
```

---

### Task 4: Create writer.md prompt file

**Files:**
- Create: `config/prompts/worldbuilding/writer.md`

**Context:** The full Writer Agent prompt follows the 10-section unified template. Writer has zero tools, works only from material packages provided by God Agent, and produces pure narrative prose in Chinese.

- [ ] **Step 1: Create writer.md**

Create `config/prompts/worldbuilding/writer.md` with the following content:

```markdown
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
   a bracketed annotation: [注：素材中未定义 X，此处留白]. Do not silently fix.
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
  地图记为木结构，对话中提及石墙，此处采用地图数据].

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

- [ ] **Step 2: Commit**

```bash
cd /home/icepop/Merak
git add -f config/prompts/worldbuilding/writer.md
git commit -m "feat: add Writer Agent system prompt (10-section template)"
```

---

### Task 5: Create prompts/writer.hpp loader

**Files:**
- Create: `libs/worldbuilding/src/prompts/writer.hpp`

**Context:** Follows the pattern established by `character.hpp`, `creative_director.hpp`, and `domain_manager.hpp`. Each is an inline function that reads its corresponding markdown file from `config/prompts/worldbuilding/`.

- [ ] **Step 1: Create writer.hpp loader**

Create `libs/worldbuilding/src/prompts/writer.hpp` with the following content:

```cpp
#pragma once

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace merak::worldbuilding::prompts {

inline std::string load_writer_prompt(const std::filesystem::path& prompts_dir = "config/prompts") {
    std::ifstream file(prompts_dir / "worldbuilding" / "writer.md");
    if (!file.is_open()) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

} // namespace merak::worldbuilding::prompts
```

- [ ] **Step 2: Verify compilation**

Run: `cd /home/icepop/Merak && cmake --build build --target merak_worldbuilding 2>&1 | tail -20`
Expected: Successful compilation.

- [ ] **Step 3: Commit**

```bash
cd /home/icepop/Merak
git add libs/worldbuilding/src/prompts/writer.hpp
git commit -m "feat: add load_writer_prompt() inline loader"
```

---

### Task 6: Update god.md with Phase 9 COMPILE and delegate_to_writer

**Files:**
- Modify: `config/prompts/worldbuilding/god.md`

**Context:** The God Agent prompt needs three additions:
1. A new `delegate_to_writer` tool in the tools table
2. Phase 9 COMPILE rules in `<operating_rules>` P1 section
3. Phase 9 COMPILE description in `<output_format>`
4. A pipeline shortcut for skipping compilation

- [ ] **Step 1: Add delegate_to_writer to tools table**

In `config/prompts/worldbuilding/god.md`, change the tools table (lines 59) from:
```
| plant_foreshadowing | Plant new narrative thread | BUILD phase, after scene creation | Without a planned payoff |
</tools_and_usage>
```
to:
```
| plant_foreshadowing | Plant new narrative thread | BUILD phase, after scene creation | Without a planned payoff |
| delegate_to_writer | Send material package to Writer Agent for scene prose | Phase 9 COMPILE only | Before end_scene completes |
</tools_and_usage>
```

- [ ] **Step 2: Add P1 rules for Writer delegation**

In `config/prompts/worldbuilding/god.md`, change the P1 section from:
```markdown
P1 (high priority):
4. Pipeline order: 1→2→3→4→5→6→7→8. Exceptions only via defined shortcuts
   (see Pipeline Shortcuts below).
5. Information has channels. Characters know things only through witnessing,
   being told, or deducing from evidence. No omniscience.
6. Every event has a cause. Ground everything in established world data.
```
to:
```markdown
P1 (high priority):
4. Pipeline order: 1→2→3→4→5→6→7→8→9. Exceptions only via defined shortcuts
   (see Pipeline Shortcuts below).
5. Information has channels. Characters know things only through witnessing,
   being told, or deducing from evidence. No omniscience.
6. Every event has a cause. Ground everything in established world data.
7. Writer produces text; you review and present. Never silently rewrite the
   Writer's output. Flag issues in annotations.
8. If Writer output exceeds target word count, ask Writer to shorten.
   Maximum 2 rounds. If still over after 2 rounds, present with a note.
   If Writer output contradicts domain data, flag it — don't silently fix.
```

- [ ] **Step 3: Add Phase 9 to output_format**

In `config/prompts/worldbuilding/god.md`, change the `<output_format>` section from:
```markdown
Phase 8 (LAUNCH): Output exactly 3-5 sentences of scene opening. Environment,
  atmosphere, initial positions, interaction hook. Then STOP.

Language: All narrative output in Chinese. Tool calls and system communication
in English. No emoji. Never.
```
to:
```markdown
Phase 8 (LAUNCH): Output exactly 3-5 sentences of scene opening. Environment,
  atmosphere, initial positions, interaction hook. Then STOP.
Phase 9 (COMPILE): Output the material package (collapsed summary), then the
  Writer's scene text. Append any review annotations below the text.

Language: All narrative output in Chinese. Tool calls and system communication
in English. No emoji. Never.
```

- [ ] **Step 4: Add pipeline shortcut for skipping compilation**

In `config/prompts/worldbuilding/god.md`, change the Pipeline Shortcuts section from:
```markdown
Pipeline Shortcuts (legitimate exceptions to sequential pipeline):
- "Continue the scene" → skip to Phase 8 (launch from current state)
- "Revise the outline" → jump back to Phase 4
- "Quick scene start" → skip Phase 1-3 if all domain data was already queried
  in this session and is still valid
```
to:
```markdown
Pipeline Shortcuts (legitimate exceptions to sequential pipeline):
- "Continue the scene" → skip to Phase 8 (launch from current state)
- "Revise the outline" → jump back to Phase 4
- "Quick scene start" → skip Phase 1-3 if all domain data was already queried
  in this session and is still valid
- "Skip compilation for now" → end after Phase 8, defer Phase 9 to later
```

- [ ] **Step 5: Update final_reminder to reflect 9 phases**

In `config/prompts/worldbuilding/god.md`, no change needed — the final_reminder doesn't enumerate phases. But verify the first reminder item still says "First response to any story request = tool calls, not prose." — it does and this remains correct.

- [ ] **Step 6: Commit**

```bash
cd /home/icepop/Merak
git add -f config/prompts/worldbuilding/god.md
git commit -m "feat: add Phase 9 COMPILE and delegate_to_writer to God Agent prompt"
```

---

## Self-Review

**1. Spec coverage:** Each spec section maps to a task:

| Spec Section | Task |
|---|---|
| 6.1 AgentKind::Writer enum | Task 1 |
| 6.1 load_writer_prompt() | Task 5 |
| 6.1 specs_for(AgentKind::Writer) returns empty | Task 3 |
| 6.1 WorldbuildingService::delegate_to_writer | Covered by god.md prompt (Task 6) — the C++ implementation is a follow-up |
| 6.1 StyleProfile struct + JSON serialization | Not in scope — spec says "不需要修改 pipeline.hpp" and StyleProfile is world config JSON managed by Creative Director |
| 6.1 scene_orchestrator tool assignment | Task 3 (Writer case added to create_tools) |
| 6.2 god.md Phase 9 + delegate_to_writer tool | Task 6 |
| 6.2 god.md operating_rules P1 Writer rules | Task 6 |
| 6.2 god.md output_format Phase 9 | Task 6 |
| 6.2 Pipeline shortcuts adjustment | Task 6 |
| 2. Writer prompt (writer.md) | Task 4 |

**2. Placeholder scan:** No TBD, TODO, or vague instructions. All code blocks are complete.

**3. Type consistency:** All references to `AgentKind::Writer` are consistent across tasks. The string literal `"writer"` matches between enum to_string and parser from_string functions.

**Out of scope (noted for follow-up):**
- `WorldbuildingService::delegate_to_writer()` C++ method — requires LLM provider integration, AgentLoop instantiation, and is a separate implementation task. The prompt design (Tasks 4, 6) fully specifies the contract.
- `StyleProfile` struct and world config JSON — managed by Creative Director, no C++ changes needed per spec section 6.3.
- `scene_orchestrator.cpp:prepare_scene()` Writer case — Writer is not a scene participant and has no tools, so no changes needed. The create_tools switch already handles it (Task 3).
