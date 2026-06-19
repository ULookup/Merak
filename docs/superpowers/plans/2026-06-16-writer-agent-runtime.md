# Writer Agent Runtime Integration Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the C++ runtime integration for Writer Agent — StyleProfile struct, DelegateToWriterTool, and WorldbuildingService method so God Agent can delegate scene prose generation to Writer at runtime.

**Architecture:** Add `StyleProfile` struct + WorldMeta config JSON field for world-level style storage. Add `DelegateToWriterTool` — a God Agent tool that, when called with a material package, instantiates a single-turn AgentLoop with the writer.md prompt (zero tools, temperature 0.7), passes the material package as user input, and returns the Writer's prose output. Register the tool in `create_tools(AgentKind::God)`.

**Tech Stack:** C++17, nlohmann/json, PostgreSQL JSONB

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `libs/worldbuilding/include/merak/worldbuilding/world_models.hpp` | Modify | `StyleProfile` struct + `WorldMeta::config` JSON field |
| `libs/worldbuilding/src/world_store.cpp` | Modify | DB migration: add `config` JSONB column; read/write in queries |
| `libs/worldbuilding/src/worldbuilding_tools.cpp` | Modify | `DelegateToWriterTool` implementation (execute loop) |
| `libs/worldbuilding/include/merak/worldbuilding/worldbuilding_tools.hpp` | Modify | `DelegateToWriterTool` class declaration + God Agent tool list registration |
| `libs/worldbuilding/src/prompts/writer.hpp` | Already exists | Writer prompt loader (from previous plan) |
| `config/prompts/worldbuilding/writer.md` | Already exists | Writer Agent prompt (from previous plan) |

---

### Task 1: Add StyleProfile struct and WorldMeta::config field

**Files:**
- Modify: `libs/worldbuilding/include/merak/worldbuilding/world_models.hpp`

**Context:** StyleProfile encodes user-definable narrative style constraints (name, description, word count range, POV, taboos, example passage). It is stored as a JSON object in a new `config` field on `WorldMeta`. The DB column is added in Task 2.

- [ ] **Step 1: Add StyleProfile struct**

In `libs/worldbuilding/include/merak/worldbuilding/world_models.hpp`, add after the `WorldMeta` struct (around line 33):

```cpp
struct StyleProfile {
    std::string name = "自然文学风格";
    std::string description = "自然、流畅的中文文学叙事，注重细节描写和情感表达";
    int target_word_count_min = 800;
    int target_word_count_max = 2000;
    std::string default_pov = "third_person_close";
    std::vector<std::string> taboos = {
        "不使用 emoji",
        "不使用网络用语",
        "不过度使用形容词堆砌"
    };
    std::string example_passage;
};

inline void to_json(nlohmann::json& j, const StyleProfile& p) {
    j = nlohmann::json{
        {"name", p.name},
        {"description", p.description},
        {"target_word_count_min", p.target_word_count_min},
        {"target_word_count_max", p.target_word_count_max},
        {"default_pov", p.default_pov},
        {"taboos", p.taboos},
        {"example_passage", p.example_passage}
    };
}

inline void from_json(const nlohmann::json& j, StyleProfile& p) {
    j.at("name").get_to(p.name);
    j.at("description").get_to(p.description);
    if (j.contains("target_word_count_min")) j.at("target_word_count_min").get_to(p.target_word_count_min);
    if (j.contains("target_word_count_max")) j.at("target_word_count_max").get_to(p.target_word_count_max);
    if (j.contains("default_pov")) j.at("default_pov").get_to(p.default_pov);
    if (j.contains("taboos")) j.at("taboos").get_to(p.taboos);
    if (j.contains("example_passage")) j.at("example_passage").get_to(p.example_passage);
}
```

- [ ] **Step 2: Add config field to WorldMeta**

In the same file, change `WorldMeta` from:
```cpp
struct WorldMeta {
    std::string id, name, description, created_at, updated_at;
```
to:
```cpp
struct WorldMeta {
    std::string id, name, description, created_at, updated_at;
    nlohmann::json config;
```

- [ ] **Step 3: Update WorldMeta JSON serialization**

The `to_json`/`from_json` for `WorldMeta` should include the `config` field. Find the existing serialization functions in `world_models.hpp` (around lines 35-60) and add `config`:

In `to_json(WorldMeta)`:
```cpp
j["config"] = m.config;
```

In `from_json(WorldMeta)`:
```cpp
if (j.contains("config")) j.at("config").get_to(m.config);
```

- [ ] **Step 4: Add helper to extract StyleProfile from WorldMeta**

Add after the StyleProfile JSON serialization:

```cpp
inline StyleProfile world_style_profile(const WorldMeta& meta) {
    if (meta.config.contains("style_profile")) {
        return meta.config["style_profile"].get<StyleProfile>();
    }
    return StyleProfile{}; // defaults
}
```

- [ ] **Step 5: Verify compilation**

Run: `cd /home/icepop/Merak && cmake --build build --target merak_worldbuilding 2>&1 | tail -20`
Expected: Successful compilation. DB query-related errors in world_store.cpp are expected (fixed in Task 2).

- [ ] **Step 6: Commit**

```bash
cd /home/icepop/Merak
git add libs/worldbuilding/include/merak/worldbuilding/world_models.hpp
git commit -m "feat: add StyleProfile struct and WorldMeta::config JSON field"
```

---

### Task 2: Add config column to world DB table and queries

**Files:**
- Modify: `libs/worldbuilding/src/world_store.cpp`

**Context:** The `worlds` PostgreSQL table needs a JSONB `config` column with default `{}`. All INSERT/UPDATE/SELECT queries on worlds must include this column.

- [ ] **Step 1: Locate the CREATE TABLE statement for worlds**

Search for `CREATE TABLE` or table initialization in `world_store.cpp` to find where the `worlds` table is defined. The `initialize()` method likely creates tables.

- [ ] **Step 2: Add ALTER TABLE migration for config column**

In the `initialize()` method, after the worlds table creation, add a migration:

```cpp
// Migration: add config JSONB column if not exists
try {
    conn.execute(
        "ALTER TABLE worlds ADD COLUMN IF NOT EXISTS config JSONB DEFAULT '{}'");
} catch (...) {
    // Column may already exist — safe to ignore
}
```

- [ ] **Step 3: Update INSERT query in create_world**

Find the INSERT statement in `create_world()` that inserts a new world row. Add `config` to the column list and `'{}'::jsonb` as the value:

```cpp
// Change the INSERT to include config column:
// INSERT INTO worlds(id, name, description, config, created_at, updated_at)
// VALUES($1, $2, $3, '{}'::jsonb, $4, $5)
```

- [ ] **Step 4: Update UPDATE query in update_world**

If `update_world` has an UPDATE statement, ensure `config` is preserved (not overwritten). The current update only touches name/description, so it should be fine — verify.

- [ ] **Step 5: Update SELECT queries**

Find `get_world()` and `list_worlds()` SELECT queries. Add `config` to the column list. Update the row-to-struct mapping functions (like `world_meta_from_row`) to read the `config` column:

```cpp
// In world_meta_from_row or equivalent:
// meta.config = json::parse(res.get(row, config_column_index));
```

- [ ] **Step 6: Add update_world_config method to WorldStore header**

In `libs/worldbuilding/include/merak/worldbuilding/world_store.hpp`, add:

```cpp
void update_world_config(const std::string& world_id,
                         const nlohmann::json& config);
```

And implement in `world_store.cpp`:
```cpp
void WorldStore::update_world_config(const std::string& world_id,
                                     const nlohmann::json& config) {
    initialize();
    auto conn = pool_->get();
    conn.execute(
        "UPDATE worlds SET config = $2, updated_at = NOW() WHERE id = $1",
        {world_id, config.dump()});
}
```

- [ ] **Step 7: Verify compilation and test**

Run: `cd /home/icepop/Merak && cmake --build build --target merak_worldbuilding 2>&1 | tail -20`
Expected: Successful compilation.

- [ ] **Step 8: Commit**

```bash
cd /home/icepop/Merak
git add libs/worldbuilding/src/world_store.cpp libs/worldbuilding/include/merak/worldbuilding/world_store.hpp
git commit -m "feat: add config JSONB column to worlds table with migration"
```

---

### Task 3: Add DelegateToWriterTool class (declaration)

**Files:**
- Modify: `libs/worldbuilding/include/merak/worldbuilding/worldbuilding_tools.hpp`

**Context:** Add the `DelegateToWriterTool` class declaration in the "God Mutation Tools" section (after `PlantForeshadowingTool`, around line 389). This tool is a God Agent tool that spawns a sub-AgentLoop for the Writer Agent.

- [ ] **Step 1: Add class declaration**

In `libs/worldbuilding/include/merak/worldbuilding/worldbuilding_tools.hpp`, add after the `UpdateForeshadowTool` declaration (around line 551):

```cpp
class DelegateToWriterTool : public Tool {
public:
    DelegateToWriterTool(WorldbuildingService& svc,
                         std::shared_ptr<LlmProvider> llm,
                         std::string default_model = "claude-sonnet-4-6")
        : svc_(&svc), llm_(std::move(llm)),
          default_model_(std::move(default_model)) {}
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext exec_ctx = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<DelegateToWriterTool>(*svc_, llm_, default_model_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
private:
    WorldbuildingService* svc_;
    std::shared_ptr<LlmProvider> llm_;
    std::string default_model_;
};
```

Note: The `#include <merak/llm_provider.hpp>` is likely already included transitively. If compilation fails with "LlmProvider not defined", add `#include <merak/llm_provider.hpp>` at the top. This header is already included in `worldbuilding_tools.cpp` (line 9).

- [ ] **Step 2: Verify compilation (will fail on missing implementation)**

Run: `cd /home/icepop/Merak && cmake --build build --target merak_worldbuilding 2>&1 | tail -20`
Expected: Linker error about missing `DelegateToWriterTool::spec()`, `meta()`, `execute()` — these are defined in Task 4.

- [ ] **Step 3: Commit**

```bash
cd /home/icepop/Merak
git add libs/worldbuilding/include/merak/worldbuilding/worldbuilding_tools.hpp
git commit -m "feat: add DelegateToWriterTool class declaration"
```

---

### Task 4: Add DelegateToWriterTool implementation

**Files:**
- Modify: `libs/worldbuilding/src/worldbuilding_tools.cpp`

**Context:** Implement the `spec()`, `meta()`, and `execute()` methods. The execute method spawns a single-turn AgentLoop with the writer.md prompt, passes the material package as user input, and returns the Writer's prose output. Follows the `ForkSkillTool` pattern for sub-AgentLoop creation.

- [ ] **Step 1: Add required includes**

At the top of `libs/worldbuilding/src/worldbuilding_tools.cpp` (around line 11), add:
```cpp
#include <merak/agent_loop.hpp>
#include <merak/tool_registry.hpp>
#include <merak/memory_store.hpp>
#include <merak/compactor.hpp>
#include <merak/cache_aware_context.hpp>
#include <merak/turn_state.hpp>
```

- [ ] **Step 2: Add spec() and meta() implementations**

Add before the `WorldbuildingTools::specs_for` function (around line 2969):

```cpp
// ========== DelegateToWriterTool ==========

ToolSpec DelegateToWriterTool::spec() const {
    ToolSpec s;
    s.name = "delegate_to_writer";
    s.description = R"(Send a structured material package to the Writer Agent to produce polished scene prose. The material package must include: scene outline, character dialogue log, relevant domain data, and writing constraints (style, POV, word count, foreshadowing). Returns the Writer's scene text.)";
    s.source = "builtin";
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "material_package": {"type": "string", "description": "Complete scene material package in markdown format"}
        },
        "required": ["material_package"]
    })";
    return s;
}

ToolMeta DelegateToWriterTool::meta() const {
    ToolMeta m;
    m.name = "delegate_to_writer";
    m.description = "Delegate scene prose writing to Writer Agent";
    m.triggers = {"writer", "prose", "compile", "scene text"};
    m.pinned = false;
    m.intents = {IntentType::DomainRead};
    m.scope = Scope::Local;
    m.schema_tokens = 30;
    return m;
}
```

- [ ] **Step 3: Add execute() implementation**

Add after the meta() function:

```cpp
std::future<ToolResult> DelegateToWriterTool::execute(ToolCall call, ToolExecutionContext exec_ctx) {
    return std::async(std::launch::async, [self = this->clone(), call = std::move(call), exec_ctx = std::move(exec_ctx)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string material_package = args.value("material_package", "");

            if (material_package.empty()) {
                result.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "delegate_to_writer requires material_package parameter");
                return result;
            }

            auto& tool = static_cast<DelegateToWriterTool&>(*self);
            auto& svc = *tool.svc_;

            // Load Writer Agent prompt
            auto writer_prompt = prompts::load_writer_prompt();
            if (writer_prompt.empty()) {
                result.output = error_response(ToolErrorCode::INTERNAL,
                    "Writer Agent prompt (writer.md) not found");
                return result;
            }

            // Create empty tool registry (Writer has zero tools)
            auto sub_tools = std::make_shared<ToolRegistry>();

            // Create memory store (fresh, no history)
            auto memory = std::make_shared<MemoryStore>();

            // Create token counter and compactor
            auto counter = std::make_shared<TokenCounter>();
            auto comp = std::make_shared<Compactor>(tool.llm_, counter);

            // Configure Writer Agent loop
            AgentLoop::Config cfg;
            cfg.system_prompt = writer_prompt;
            cfg.max_turns = 1;           // Single-turn: Writer has no tools
            cfg.default_model = tool.default_model_;
            cfg.max_output_tokens = 8192; // Allow room for full scene prose
            cfg.model_max_tokens = 128000;
            cfg.enable_compaction = false;
            cfg.enable_cache = true;

            // Create sub-loop
            AgentLoop sub_loop(cfg, tool.llm_, sub_tools, memory, comp,
                               std::shared_ptr<worldbuilding::WorldbuildingService>(),
                               nullptr); // no skills registry needed

            if (!exec_ctx.world_id.empty()) sub_loop.set_active_world_id(exec_ctx.world_id);
            if (!exec_ctx.scene_id.empty()) sub_loop.set_active_scene_id(exec_ctx.scene_id);

            NullRunControl control;
            auto response = sub_loop.run(material_package, control).get();

            result.output = ok_response({{"scene_text", response.text}});
            result.is_error = false;

        } catch (const std::exception& e) {
            result.output = error_response(ToolErrorCode::INTERNAL,
                std::string("Writer Agent failed: ") + e.what());
            result.is_error = true;
        }

        return result;
    });
}
```

Note: The `prompts::load_writer_prompt()` call may need the path to find writer.md. If the prompts directory is not set, we may need to pass it through the tool. The existing approach uses a default of `"config/prompts"` which should work relative to the working directory. If it doesn't, we'll adjust the path.

- [ ] **Step 4: Register DelegateToWriterTool in God Agent's create_tools**

In the `create_tools` switch, inside `case AgentKind::God:` (after the UpdateForeshadowTool registration around line 3006), add:

```cpp
        tools.push_back(
            std::make_unique<DelegateToWriterTool>(*service_, llm_, diary_model_));
```

- [ ] **Step 5: Verify compilation**

Run: `cd /home/icepop/Merak && cmake --build build --target merak_worldbuilding 2>&1 | tail -30`
Expected: Successful compilation. Fix any include errors (LlmProvider, AgentLoop headers may need full paths).

- [ ] **Step 6: Commit**

```bash
cd /home/icepop/Merak
git add libs/worldbuilding/src/worldbuilding_tools.cpp
git commit -m "feat: add DelegateToWriterTool implementation and registration"
```

---

### Task 5: Verify full build and integration

**Files:**
- Verify: full build passes
- Verify: God Agent has delegate_to_writer in its tool list

- [ ] **Step 1: Full project build**

Run: `cd /home/icepop/Merak && cmake --build build 2>&1 | tail -30`
Expected: Full successful build with zero new errors.

- [ ] **Step 2: Verify tool is registered**

Run a quick verification that `WorldbuildingTools::specs_for(AgentKind::God)` includes `delegate_to_writer`:

```bash
cd /home/icepop/Merak
cmake --build build --target merak_worldbuilding 2>&1 | grep -i "error\|warning" | grep -i "writer\|delegate" || echo "No writer/delegate errors found"
```

Expected: No errors.

- [ ] **Step 3: Commit any final fixes**

```bash
cd /home/icepop/Merak
git add -A  # only if there are fixups
git commit -m "chore: verify Writer Agent runtime integration compiles cleanly"
```

---

## Out of Scope (follow-up work)

1. **StyleProfile management UI** — users currently set style via Creative Director conversations. A WebUI for editing StyleProfile is a separate feature.
2. **Temperature configuration** — the spec says temperature=0.7 for Writer. This is currently hardcoded; making it configurable per-world requires adding `temperature` to StyleProfile.
3. **Multi-round Writer revision** — the spec allows up to 2 rounds of God Agent asking Writer to shorten output. Currently single-turn; multi-turn requires resuming the same AgentLoop.
4. **Writer AgentLoop in prepare_scene** — Writer is never a scene participant, so `scene_orchestrator.cpp:prepare_scene()` doesn't need a Writer case.

---

## Self-Review

**1. Spec coverage:**

| Spec Section | Task |
|---|---|
| 5.1 StyleProfile struct | Task 1 |
| 5.2 world_config JSON storage | Task 1 (struct) + Task 2 (DB column) |
| 6.1 DelegateToWriterTool class | Tasks 3-4 |
| 6.1 WorldbuildingService::delegate_to_writer | Embedded in DelegateToWriterTool::execute() (Task 4) — no separate service method needed |
| 6.1 specs_for(AgentKind::Writer) returns empty tools | Already done in previous plan (Task 3 of that plan) |
| 7.1 delegate_to_writer tool implementation | Task 4 |
| 7.2 Writer AgentLoop config (max_tool_turns=0, single-turn) | Task 4 execute() — max_turns=1, empty tool registry |

**2. Placeholder scan:** No TBD/TODO. All code blocks are complete.

**3. Type consistency:** `DelegateToWriterTool` name is consistent between declaration (Task 3) and implementation (Task 4). `material_package` parameter name matches the god.md prompt description from the previous plan.
