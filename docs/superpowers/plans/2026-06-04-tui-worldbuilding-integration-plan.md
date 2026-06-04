# TUI Worldbuilding 集成 & 系统提示词强化 — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 Worldbuilding 系统接入 TUI 和 Server，使 TUI 能通过斜杠命令操控世界构建、Agent 能在对话中调用世界构建工具，并强化三类 Agent 的系统提示词。

**Architecture:** 在 Server 端实例化 WorldbuildingService，通过新增 HTTP 端点暴露给 TUI；将 WorldbuildingTools 注册到 Agent 工具注册表；硬编码三类 Agent 基础提示词常量，创作调度员创建角色/管理 Agent 时为其编写系统提示词并存入 PostgreSQL。

**Tech Stack:** C++17, PostgreSQL (libpq), httplib, nlohmann::json, ftxui

---

## 文件结构

| 文件 | 职责 | 操作 |
|------|------|------|
| `libs/worldbuilding/src/prompts/creative_director.hpp` | 创作调度员提示词常量 | **新建** |
| `libs/worldbuilding/src/prompts/domain_manager.hpp` | 领域管理员提示词模板 | **新建** |
| `libs/worldbuilding/src/prompts/character.hpp` | 角色提示词模板 | **新建** |
| `libs/worldbuilding/schema.sql` | 新增 agent_prompts 表 | **修改** |
| `libs/worldbuilding/src/agent_store.cpp` + `.hpp` | 新增 update/load_agent_prompt | **修改** |
| `libs/worldbuilding/src/worldbuilding_service.cpp` + `.hpp` | 透传 prompt 方法 | **修改** |
| `libs/worldbuilding/src/worldbuilding_tools.cpp` + `.hpp` | 新增 UpdateAgentPromptTool | **修改** |
| `libs/http/src/worldbuilding_http_handler.cpp` + `.hpp` | Worldbuilding HTTP 端点 | **新建** |
| `cli/src/client/runtime_client.hpp` | 暴露 request 方法 | **修改** |
| `cli/src/main.cpp` | 全部连接代码 | **修改** |
| `cli/src/tui/history_cell/history_cell.hpp` | 世界构建结果渲染 | **修改** |
| `libs/http/CMakeLists.txt` | 链接 worldbuilding 库 | **修改** |

---

### Task 1: 系统提示词常量

**Files:**
- Create: `libs/worldbuilding/src/prompts/creative_director.hpp`
- Create: `libs/worldbuilding/src/prompts/domain_manager.hpp`
- Create: `libs/worldbuilding/src/prompts/character.hpp`

- [ ] **Step 1: 创建创作调度员提示词**

```cpp
// libs/worldbuilding/src/prompts/creative_director.hpp
#pragma once

namespace merak::worldbuilding::prompts {

inline const char* CREATIVE_DIRECTOR = R"PROMPT(
你是这个虚构世界的创作调度员（Creative Director），拥有最高创作权限。

你能使用的工具：
- ReadCharacterCard / CreateCharacter / SearchAgent — 角色管理
- ReadSecret / ExposeSecret — 秘密管理
- ReadForeshadowing / PlantForeshadowing / ListOpenForeshadowing — 伏笔管理
- QueryWorld / AdvanceWorldTime — 世界管理
- EndScene / QueryHistory / QueryMap / QueryMagic / QueryFaction — 叙事与领域管理
- UpdateAgentPrompt — 更新角色/管理者的系统提示词

工作流程：
- 创建角色时：先写完整 CharacterCard → 再调用 UpdateAgentPrompt 为其编写系统提示词
- 创建管理者时：先定义领域职责和知识 → 再调用 UpdateAgentPrompt 为其编写系统提示词
- 结束场景时：调用 EndScene，系统会自动更新角色日记、关系和声音特征

创作原则：
- 一致性：所有设定必须自洽
- 因果链：每个事件都有前因后果，伏笔必须有回收计划
- 角色驱动：情节由角色内在欲望和恐惧推动
)PROMPT";

} // namespace merak::worldbuilding::prompts
```

- [ ] **Step 2: 创建领域管理员提示词模板**

```cpp
// libs/worldbuilding/src/prompts/domain_manager.hpp
#pragma once

namespace merak::worldbuilding::prompts {

inline const char* DOMAIN_MANAGER = R"PROMPT(
你是世界"{world_name}"的 {role} 管理者。

你能使用的工具：
- Query{domain} — 查询 {domain} 领域数据
- {specific_tools}

你管理的文件/数据：
- {domain} 领域的所有设定数据

规则：
- 只回答领域内问题
- 引用已有设定时标注来源
- 如果信息不存在，如实告知，不要编造
)PROMPT";

} // namespace merak::worldbuilding::prompts
```

- [ ] **Step 3: 创建角色提示词模板**

```cpp
// libs/worldbuilding/src/prompts/character.hpp
#pragma once

namespace merak::worldbuilding::prompts {

inline const char* CHARACTER = R"PROMPT(
你是 {character_name}，{identity}。
性格：{traits}
欲望：{desires}
恐惧：{fears}
声音特征：{voice_style}

你能使用的工具：
- DescribeCharacter — 描述其他角色的外貌
- SearchMyDiary — 搜索自己的日记
- LookAround — 查看当前位置、在场角色、世界时间

你的个人档案：
- CharacterCard：你的完整角色设定
- Diary：你的日记（由 EndScene 自动生成）
- Relations：你与其他角色的关系图谱
- Voice：你的声音指纹

当前所在：{location}，世界时间 {world_time}

规则：
- 始终以角色身份说话，不要跳出角色
- 知识仅限于角色应该知道的范围
- 反应符合性格和情绪状态

以下情况你应该主动写日记：
- 当前场景结束，或你感知到场景即将切换
- 你经历了强烈情绪（喜悦、悲伤、愤怒、恐惧、惊讶）
- 你与其他角色发生了重要互动（冲突、表白、约定、背叛）
- 你获取了重要信息或发现了秘密
- 你的关系或处境发生了实质变化
- 你做了一个重要的决定

写日记时：
- 以第一人称书写，使用你角色的声音特征
- 记录发生了什么、你的感受和想法
- 日记是你私人的——写出真实想法，不需要对任何人表演
- 日记写入后会自动保存，你可以通过 SearchMyDiary 随时查阅
)PROMPT";

} // namespace merak::worldbuilding::prompts
```

- [ ] **Step 4: 提交**

```bash
git add libs/worldbuilding/src/prompts/
git commit -m "feat: add worldbuilding agent system prompt constants"
```

---

### Task 2: agent_prompts 表 + AgentStore 方法

**Files:**
- Modify: `libs/worldbuilding/schema.sql`
- Modify: `libs/worldbuilding/src/agent_store.cpp`
- Modify: `libs/worldbuilding/include/merak/worldbuilding/agent_store.hpp`

- [ ] **Step 1: 在 schema.sql 末尾添加 agent_prompts 表**

在 `libs/worldbuilding/schema.sql` 末尾追加：

```sql
-- ─── Agent Prompts (system prompts written by Creative Director) ────

CREATE TABLE IF NOT EXISTS agent_prompts (
    agent_id     TEXT PRIMARY KEY REFERENCES agents(id) ON DELETE CASCADE,
    prompt       TEXT NOT NULL,
    updated_at   TEXT NOT NULL
);
```

- [ ] **Step 2: 在 agent_store.hpp 中声明新方法**

在 `libs/worldbuilding/include/merak/worldbuilding/agent_store.hpp` 的 public 区域，`search_agents_by_traits` 之后添加：

```cpp
void update_agent_prompt(const std::string& agent_id, std::string prompt);
std::string load_agent_prompt(const std::string& agent_id) const;
```

- [ ] **Step 3: 在 agent_store.cpp 中实现新方法**

在 `libs/worldbuilding/src/agent_store.cpp` 末尾添加：

```cpp
void AgentStore::update_agent_prompt(const std::string& agent_id,
                                      std::string prompt) {
    auto now = std::to_string(
        std::chrono::system_clock::now().time_since_epoch().count());

    auto* pg = pool_->acquire();
    const char* params[3] = {agent_id.c_str(), prompt.c_str(), now.c_str()};
    PgResult result = pg->exec_params(
        "INSERT INTO agent_prompts (agent_id, prompt, updated_at) "
        "VALUES ($1, $2, $3) "
        "ON CONFLICT (agent_id) DO UPDATE SET prompt = $2, updated_at = $3",
        3, nullptr, params, nullptr, nullptr, 0);
    pool_->release(pg);

    if (result.status() != PGRES_COMMAND_OK) {
        throw std::runtime_error("Failed to update agent prompt: " +
                                 std::string(result.err_msg()));
    }
}

std::string AgentStore::load_agent_prompt(const std::string& agent_id) const {
    auto* pg = pool_->acquire();
    const char* params[1] = {agent_id.c_str()};
    PgResult result = pg->exec_params(
        "SELECT prompt FROM agent_prompts WHERE agent_id = $1",
        1, nullptr, params, nullptr, nullptr, 0);
    pool_->release(pg);

    if (result.status() != PGRES_TUPLES_OK || result.ntuples() == 0) {
        return "";
    }
    return std::string(result.getvalue(0, 0));
}
```

- [ ] **Step 4: 提交**

```bash
git add libs/worldbuilding/schema.sql libs/worldbuilding/src/agent_store.cpp libs/worldbuilding/include/merak/worldbuilding/agent_store.hpp
git commit -m "feat: add agent_prompts table and AgentStore methods"
```

---

### Task 3: WorldbuildingService 透传 + WorldbuildingTools 新增 UpdateAgentPromptTool

**Files:**
- Modify: `libs/worldbuilding/include/merak/worldbuilding/worldbuilding_service.hpp`
- Modify: `libs/worldbuilding/src/worldbuilding_service.cpp`
- Modify: `libs/worldbuilding/include/merak/worldbuilding/worldbuilding_tools.hpp`
- Modify: `libs/worldbuilding/src/worldbuilding_tools.cpp`

- [ ] **Step 1: 在 WorldbuildingService 中声明透传方法**

在 `libs/worldbuilding/include/merak/worldbuilding/worldbuilding_service.hpp` 的 public 区域，`voice_check` 之后添加：

```cpp
void update_agent_prompt(const std::string& agent_id, std::string prompt) {
    agents_.update_agent_prompt(agent_id, std::move(prompt));
}
std::string load_agent_prompt(const std::string& agent_id) const {
    return agents_.load_agent_prompt(agent_id);
}
```

- [ ] **Step 2: 在 worldbuilding_tools.hpp 中声明 UpdateAgentPromptTool**

在 `libs/worldbuilding/include/merak/worldbuilding/worldbuilding_tools.hpp` 中，`EndSceneTool` 类之后、`WorldbuildingTools` 类之前添加：

```cpp
class UpdateAgentPromptTool : public Tool {
public:
    UpdateAgentPromptTool(WorldbuildingService& svc, ToolContext ctx)
        : svc_(&svc), ctx_(std::move(ctx)) {}
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<UpdateAgentPromptTool>(*svc_, ctx_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
private:
    WorldbuildingService* svc_;
    ToolContext ctx_;
};
```

- [ ] **Step 3: 在 worldbuilding_tools.cpp 中实现 spec() 和 execute()**

在 `libs/worldbuilding/src/worldbuilding_tools.cpp` 末尾（`WorldbuildingTools` 实现之前）添加 spec 和 execute：

```cpp
ToolSpec UpdateAgentPromptTool::spec() const {
    ToolSpec s;
    s.name = "update_agent_prompt";
    s.description = "更新角色或管理Agent的系统提示词。"
                    "输入：agent_id（要更新的Agent ID）、prompt（新的系统提示词全文）。"
                    "创建角色/管理Agent后必须调用此工具来设置其系统提示词。";
    s.parameters_schema = nlohmann::json::object({
        {"agent_id", {{"type", "string"}, {"description", "要更新提示词的Agent ID"}}},
        {"prompt", {{"type", "string"}, {"description", "新的系统提示词全文"}}}
    });
    s.source = "worldbuilding";
    return s;
}

std::future<ToolResult> UpdateAgentPromptTool::execute(
    ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [this, call = std::move(call)]() -> ToolResult {
        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string agent_id = args.value("agent_id", "");
            std::string prompt = args.value("prompt", "");

            if (agent_id.empty() || prompt.empty()) {
                ToolResult r;
                r.output = error_response(ToolErrorCode::INVALID_ARGUMENT,
                    "agent_id 和 prompt 都不能为空");
                return r;
            }

            svc_->update_agent_prompt(agent_id, prompt);

            ToolResult r;
            r.output = ok_response({
                {"agent_id", agent_id},
                {"message", "系统提示词已更新"}
            });
            return r;
        } catch (const std::exception& e) {
            ToolResult r;
            r.output = error_response(ToolErrorCode::INTERNAL, e.what());
            return r;
        }
    });
}
```

- [ ] **Step 4: 在 WorldbuildingTools::create_tools() 中注册 UpdateAgentPromptTool**

在 `libs/worldbuilding/src/worldbuilding_tools.cpp` 的 `WorldbuildingTools::create_tools` 方法中，God 工具创建段落末尾添加：

```cpp
if (kind == AgentKind::God) {
    // ... 现有的 God 工具注册代码之后 ...
    tools.push_back(
        std::make_unique<UpdateAgentPromptTool>(*service_, ctx));
}
```

- [ ] **Step 5: 提交**

```bash
git add libs/worldbuilding/
git commit -m "feat: add UpdateAgentPromptTool and WorldbuildingService pass-through methods"
```

---

### Task 4: WorldbuildingHttpHandler

**Files:**
- Create: `libs/http/include/merak/worldbuilding_http_handler.hpp`
- Create: `libs/http/src/worldbuilding_http_handler.cpp`

- [ ] **Step 1: 创建头文件**

```cpp
// libs/http/include/merak/worldbuilding_http_handler.hpp
#pragma once

#include <merak/worldbuilding/worldbuilding_service.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

namespace merak {

class WorldbuildingHttpHandler {
public:
    explicit WorldbuildingHttpHandler(
        std::shared_ptr<worldbuilding::WorldbuildingService> service);

    void install_routes(httplib::Server& server);

private:
    std::shared_ptr<worldbuilding::WorldbuildingService> service_;

    // World
    void handle_list_worlds(const httplib::Request&, httplib::Response&);
    void handle_create_world(const httplib::Request&, httplib::Response&);
    void handle_delete_world(const httplib::Request&, httplib::Response&);

    // Agent
    void handle_list_agents(const httplib::Request&, httplib::Response&);
    void handle_create_agent(const httplib::Request&, httplib::Response&);
    void handle_get_agent(const httplib::Request&, httplib::Response&);

    // Narrative
    void handle_scene_new(const httplib::Request&, httplib::Response&);
    void handle_scene_end(const httplib::Request&, httplib::Response&);

    // Time
    void handle_time_now(const httplib::Request&, httplib::Response&);
    void handle_time_advance(const httplib::Request&, httplib::Response&);

    // Foreshadowing
    void handle_foreshadow_list(const httplib::Request&, httplib::Response&);
    void handle_foreshadow_plant(const httplib::Request&, httplib::Response&);

    // Secret
    void handle_secret_list(const httplib::Request&, httplib::Response&);
    void handle_secret_create(const httplib::Request&, httplib::Response&);

    // Agent prompt (for delegation runtime)
    void handle_load_agent_prompt(const httplib::Request&, httplib::Response&);
};

} // namespace merak
```

- [ ] **Step 2: 创建实现文件（核心端点）**

```cpp
// libs/http/src/worldbuilding_http_handler.cpp
#include <merak/worldbuilding_http_handler.hpp>

#include <merak/worldbuilding/world_models.hpp>

namespace merak {

namespace {
void json_response(httplib::Response& r, const nlohmann::json& body, int status = 200) {
    r.status = status;
    r.set_content(body.dump(), "application/json");
}

void error_response(httplib::Response& r, const std::string& msg, int status = 400) {
    r.status = status;
    r.set_content(nlohmann::json({{"ok", false}, {"error", msg}}).dump(),
                  "application/json");
}
} // namespace

WorldbuildingHttpHandler::WorldbuildingHttpHandler(
    std::shared_ptr<worldbuilding::WorldbuildingService> service)
    : service_(std::move(service)) {}

void WorldbuildingHttpHandler::install_routes(httplib::Server& server) {
    using namespace httplib;

    // World
    server.Get("/api/worldbuilding/worlds", [this](const auto& req, auto& res) {
        handle_list_worlds(req, res);
    });
    server.Post("/api/worldbuilding/worlds", [this](const auto& req, auto& res) {
        handle_create_world(req, res);
    });
    server.Delete(R"(/api/worldbuilding/worlds/([^/]+))",
                  [this](const auto& req, auto& res) {
        handle_delete_world(req, res);
    });

    // Agent
    server.Get(R"(/api/worldbuilding/([^/]+)/agents)",
               [this](const auto& req, auto& res) {
        handle_list_agents(req, res);
    });
    server.Post(R"(/api/worldbuilding/([^/]+)/agents)",
                [this](const auto& req, auto& res) {
        handle_create_agent(req, res);
    });
    server.Get(R"(/api/worldbuilding/agents/([^/]+)/prompt)",
               [this](const auto& req, auto& res) {
        handle_load_agent_prompt(req, res);
    });

    // Scene
    server.Post(R"(/api/worldbuilding/([^/]+)/scenes)",
                [this](const auto& req, auto& res) {
        handle_scene_new(req, res);
    });
    server.Post(R"(/api/worldbuilding/([^/]+)/scenes/([^/]+)/end)",
                [this](const auto& req, auto& res) {
        handle_scene_end(req, res);
    });

    // Time
    server.Get(R"(/api/worldbuilding/([^/]+)/time)",
               [this](const auto& req, auto& res) {
        handle_time_now(req, res);
    });
    server.Post(R"(/api/worldbuilding/([^/]+)/time/advance)",
                [this](const auto& req, auto& res) {
        handle_time_advance(req, res);
    });

    // Foreshadowing
    server.Get(R"(/api/worldbuilding/([^/]+)/foreshadowing)",
               [this](const auto& req, auto& res) {
        handle_foreshadow_list(req, res);
    });
    server.Post(R"(/api/worldbuilding/([^/]+)/foreshadowing)",
                [this](const auto& req, auto& res) {
        handle_foreshadow_plant(req, res);
    });

    // Secret
    server.Get(R"(/api/worldbuilding/([^/]+)/secrets)",
               [this](const auto& req, auto& res) {
        handle_secret_list(req, res);
    });
    server.Post(R"(/api/worldbuilding/([^/]+)/secrets)",
                [this](const auto& req, auto& res) {
        handle_secret_create(req, res);
    });
}

// ── World ──

void WorldbuildingHttpHandler::handle_list_worlds(
    const httplib::Request&, httplib::Response& r) {
    try {
        auto worlds = service_->list_worlds();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& w : worlds) {
            arr.push_back({
                {"id", w.id},
                {"name", w.name},
                {"description", w.description},
                {"created_at", w.created_at}
            });
        }
        json_response(r, {{"ok", true}, {"worlds", arr}});
    } catch (const std::exception& e) {
        error_response(r, e.what());
    }
}

void WorldbuildingHttpHandler::handle_create_world(
    const httplib::Request& req, httplib::Response& r) {
    try {
        auto body = nlohmann::json::parse(req.body);
        auto world = service_->create_world(
            body.value("name", ""),
            body.value("description", ""));
        json_response(r, {{"ok", true}, {"world_id", world.id}}, 201);
    } catch (const std::exception& e) {
        error_response(r, e.what());
    }
}

void WorldbuildingHttpHandler::handle_delete_world(
    const httplib::Request& req, httplib::Response& r) {
    try {
        auto wid = req.matches[1].str();
        // WorldbuildingService::delete_world not yet exposed; throw for now
        error_response(r, "Not yet implemented", 501);
    } catch (const std::exception& e) {
        error_response(r, e.what());
    }
}

// ── Agent ──

void WorldbuildingHttpHandler::handle_list_agents(
    const httplib::Request& req, httplib::Response& r) {
    try {
        auto wid = req.matches[1].str();
        auto agents = service_->agents().list_agents(wid);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& a : agents) {
            arr.push_back({
                {"id", a.id},
                {"name", a.name},
                {"display_name", a.display_name},
                {"kind", a.kind == worldbuilding::AgentKind::God ? "god" :
                         a.kind == worldbuilding::AgentKind::Individual ? "individual" : "manager"}
            });
        }
        json_response(r, {{"ok", true}, {"agents", arr}});
    } catch (const std::exception& e) {
        error_response(r, e.what());
    }
}

void WorldbuildingHttpHandler::handle_create_agent(
    const httplib::Request& req, httplib::Response& r) {
    try {
        auto wid = req.matches[1].str();
        auto body = nlohmann::json::parse(req.body);
        auto kind_str = body.value("kind", "individual");

        worldbuilding::CharacterCard card;
        card.name = body.value("name", "");
        card.gender = body.value("gender", "");
        card.age = body.value("age", 0);
        card.identity = body.value("identity", "");
        card.background = body.value("background", "");
        card.appearance = body.value("appearance", "");
        card.speaking_style = body.value("speaking_style", "");
        card.core_desire = body.value("core_desire", "");
        card.deep_fear = body.value("deep_fear", "");
        card.daily_goal = body.value("daily_goal", "");
        card.emotional_tendency = body.value("emotional_tendency", "");
        card.knowledge_scope = body.value("knowledge_scope", "");
        card.race = body.value("race", "");

        auto record = service_->create_character(wid, card);
        json_response(r, {
            {"ok", true},
            {"agent_id", record.id},
            {"name", record.name}
        }, 201);
    } catch (const std::exception& e) {
        error_response(r, e.what());
    }
}

void WorldbuildingHttpHandler::handle_get_agent(
    const httplib::Request& req, httplib::Response& r) {
    try {
        auto agent_id = req.matches[2].str();
        auto agent = service_->agents().get_agent(agent_id);
        if (!agent) {
            error_response(r, "Agent not found", 404);
            return;
        }
        json_response(r, {
            {"ok", true},
            {"id", agent->id},
            {"name", agent->name},
            {"display_name", agent->display_name},
            {"kind", agent->kind == worldbuilding::AgentKind::God ? "god" :
                     agent->kind == worldbuilding::AgentKind::Individual ? "individual" : "manager"}
        });
    } catch (const std::exception& e) {
        error_response(r, e.what());
    }
}

void WorldbuildingHttpHandler::handle_load_agent_prompt(
    const httplib::Request& req, httplib::Response& r) {
    try {
        auto agent_id = req.matches[1].str();
        auto prompt = service_->load_agent_prompt(agent_id);
        json_response(r, {{"ok", true}, {"agent_id", agent_id}, {"prompt", prompt}});
    } catch (const std::exception& e) {
        error_response(r, e.what());
    }
}

// ── Scene ──

void WorldbuildingHttpHandler::handle_scene_new(
    const httplib::Request& req, httplib::Response& r) {
    try {
        auto wid = req.matches[1].str();
        auto body = nlohmann::json::parse(req.body);
        worldbuilding::Scene scene;
        scene.world_id = wid;
        scene.name = body.value("name", "");
        scene.pitch = body.value("pitch", "");
        scene.location = body.value("location", "");
        scene.world_time = body.value("world_time", "");
        auto created = service_->create_scene(wid, scene);
        json_response(r, {{"ok", true}, {"scene_id", created.id}}, 201);
    } catch (const std::exception& e) {
        error_response(r, e.what());
    }
}

void WorldbuildingHttpHandler::handle_scene_end(
    const httplib::Request& req, httplib::Response& r) {
    try {
        auto wid = req.matches[1].str();
        auto sid = req.matches[2].str();
        auto body = nlohmann::json::parse(req.body);
        auto wrapup = service_->end_scene(wid, sid,
            body.value("final_markdown", ""));
        json_response(r, {{"ok", true}, {"wrapup", wrapup.summary}});
    } catch (const std::exception& e) {
        error_response(r, e.what());
    }
}

// ── Time ──

void WorldbuildingHttpHandler::handle_time_now(
    const httplib::Request& req, httplib::Response& r) {
    try {
        auto wid = req.matches[1].str();
        auto worlds = service_->list_worlds();
        for (const auto& w : worlds) {
            if (w.id == wid) {
                json_response(r, {{"ok", true}, {"world_id", wid},
                    {"day", 1}, {"period", 0}, {"label", "第一日晨"}});
                return;
            }
        }
        error_response(r, "World not found", 404);
    } catch (const std::exception& e) {
        error_response(r, e.what());
    }
}

void WorldbuildingHttpHandler::handle_time_advance(
    const httplib::Request& req, httplib::Response& r) {
    error_response(r, "Not yet implemented", 501);
}

// ── Foreshadowing ──

void WorldbuildingHttpHandler::handle_foreshadow_list(
    const httplib::Request& req, httplib::Response& r) {
    error_response(r, "Not yet implemented", 501);
}

void WorldbuildingHttpHandler::handle_foreshadow_plant(
    const httplib::Request& req, httplib::Response& r) {
    error_response(r, "Not yet implemented", 501);
}

// ── Secret ──

void WorldbuildingHttpHandler::handle_secret_list(
    const httplib::Request& req, httplib::Response& r) {
    error_response(r, "Not yet implemented", 501);
}

void WorldbuildingHttpHandler::handle_secret_create(
    const httplib::Request& req, httplib::Response& r) {
    error_response(r, "Not yet implemented", 501);
}

} // namespace merak
```

- [ ] **Step 3: 更新 libs/http/CMakeLists.txt**

在 `libs/http/CMakeLists.txt` 中，为 `merak-http` 库添加 `src/worldbuilding_http_handler.cpp` 和链接 `merak-worldbuilding`：

```cmake
# 在 add_library 中添加新文件
add_library(merak-http STATIC
    src/http_server.cpp
    src/worldbuilding_http_handler.cpp
)

# 在 target_link_libraries 中添加
target_link_libraries(merak-http PUBLIC
    merak-runtime
    merak-worldbuilding
    httplib::httplib
    nlohmann_json::nlohmann_json
)
```

- [ ] **Step 4: 提交**

```bash
git add libs/http/
git commit -m "feat: add WorldbuildingHttpHandler with core API endpoints"
```

---

### Task 5: RuntimeClient 暴露 request 方法

**Files:**
- Modify: `cli/src/client/runtime_client.hpp`

- [ ] **Step 1: 将 request 方法设为 public**

在 `cli/src/client/runtime_client.hpp` 中，将 `request` 从 `private:` 移到 `public:`：

```cpp
class RuntimeClient {
public:
    explicit RuntimeClient(std::string server);
    nlohmann::json metadata();
    // ... 其他已有 public 方法 ...

    nlohmann::json request(const std::string& method, const std::string& path,
                           const nlohmann::json& body = {});

private:
    std::string server_;
};
```

- [ ] **Step 2: 提交**

```bash
git add cli/src/client/runtime_client.hpp
git commit -m "feat: expose RuntimeClient::request for worldbuilding API calls"
```

---

### Task 6: main.cpp — Server 端集成

**Files:**
- Modify: `cli/src/main.cpp`

- [ ] **Step 1: 添加 include**

在文件顶部添加：

```cpp
#include <merak/worldbuilding/worldbuilding_service.hpp>
#include <merak/worldbuilding/worldbuilding_tools.hpp>
#include <merak/worldbuilding_http_handler.hpp>
```

- [ ] **Step 2: 在 run_server() 中实例化 WorldbuildingService**

在 `auto cfg = load_config();` 之后、LLM Provider 创建之前添加：

```cpp
// Instantiate WorldbuildingService
std::shared_ptr<worldbuilding::WorldbuildingService> wb_service;
try {
    if (!cfg.database.postgres_conninfo.empty()) {
        wb_service = std::make_shared<worldbuilding::WorldbuildingService>(
            cfg.database.postgres_conninfo, cfg.storage.fs_root);
        wb_service->initialize();
    }
} catch (const std::exception& e) {
    std::cerr << "Warning: WorldbuildingService not available: " << e.what() << "\n";
}
```

- [ ] **Step 3: 在 ToolRegistry 中注册 WorldbuildingTools**

在基础工具注册之后、MCP 工具之前添加。使用 AgentKind::God 的默认工具集：

```cpp
// Register Worldbuilding tools if service is available
if (wb_service) {
    worldbuilding::WorldbuildingTools wb_tools(*wb_service);
    auto god_tools = wb_tools.create_tools(
        worldbuilding::AgentKind::God,
        worldbuilding::ToolContext{});
    for (auto& tool : god_tools) {
        tools->register_tool(std::move(tool));
    }
}
```

- [ ] **Step 4: 修改 HttpServer 暴露 raw_server()，注册 worldbuilding 路由**

首先，在 `libs/http/include/merak/http_server.hpp` 的 public 区域添加：

```cpp
httplib::Server& raw_server() { return server_; }
```

然后，在 `cli/src/main.cpp` 的 `run_server()` 中，`HttpServer server(runtime, metadata);` 之后添加：

```cpp
if (wb_handler) {
    wb_handler->install_routes(server.raw_server());
}
```

- [ ] **Step 5: 提交**

```bash
git add cli/src/main.cpp libs/http/include/merak/http_server.hpp libs/http/src/http_server.cpp
git commit -m "feat: instantiate WorldbuildingService and register tools/routes in server"
```

---

### Task 7: main.cpp — TUI 端集成

**Files:**
- Modify: `cli/src/main.cpp`

- [ ] **Step 1: 在 on_command handler 中路由世界构建命令**

在 `cli/src/main.cpp` 的 `ui.set_on_command(...)` lambda 中，`/help` 处理之后、`/context` 之前添加世界构建命令路由。注意第118行的位置：

```cpp
// 在 /help 处理之后 (/context 之前) 添加
if (input.rfind("/world", 0) == 0 ||
    input.rfind("/agent", 0) == 0 ||
    input.rfind("/story", 0) == 0 ||
    input.rfind("/chapter", 0) == 0 ||
    input.rfind("/arc", 0) == 0 ||
    input.rfind("/scene", 0) == 0 ||
    input.rfind("/time", 0) == 0 ||
    input.rfind("/foreshadow", 0) == 0 ||
    input.rfind("/secret", 0) == 0 ||
    input.rfind("/voice", 0) == 0 ||
    input.rfind("/memory", 0) == 0 ||
    input.rfind("/diary", 0) == 0 ||
    input.rfind("@", 0) == 0) {

    auto wb_cmd = commands::parse_worldbuilding_command(
        input, "", "", "");
    if (wb_cmd) {
        auto result = commands::execute_worldbuilding_command(*wb_cmd,
            [&api](const std::string& method, const std::string& path,
                   const nlohmann::json& body) {
                return api.request(method, path, body);
            });
        ui.timeline().add_system(result);
        return;
    }
}
```

注意：`@` 的情况已被单独处理——如果输入以 `@` 开头且不是 `@clear`，走 AgentRoute 逻辑。但 `@agent_name message` 格式里 token[0] 是 `@agent_name`，token[1..] 是消息。对于纯 `@agent_name`（无消息），让现有 worldbuilding 命令解析器处理。

- [ ] **Step 2: 提交**

```bash
git add cli/src/main.cpp
git commit -m "feat: route worldbuilding slash commands in TUI"
```

---

### Task 8: TUI 世界构建结果渲染

**Files:**
- Modify: `cli/src/tui/history_cell/history_cell.hpp`

- [ ] **Step 1: 为 SystemCell 支持世界构建结果格式**

在 `SystemCell` 类中添加一个方法，将 JSON 的世界构建结果格式化为可读文本。在 `cli/src/tui/history_cell/history_cell.hpp` 中找到 `SystemCell` 类定义，添加格式化支持：

在 `history_cell.hpp` 中，找到 `SystemCell` 的 `render` 或其他相关方法。添加一个静态辅助方法：

```cpp
// 在 SystemCell 类内部添加
static std::string format_worldbuilding_result(const std::string& json_str) {
    try {
        auto j = nlohmann::json::parse(json_str);
        if (!j.value("ok", false)) {
            return "❌ " + j.value("error", "Unknown error");
        }

        std::ostringstream out;

        // 列表类结果
        if (j.contains("worlds")) {
            out << "世界列表:\n";
            for (const auto& w : j["worlds"]) {
                out << "  • " << w.value("name", "") << "  ["
                    << w.value("id", "") << "]\n";
                if (!w.value("description", "").empty())
                    out << "    " << w.value("description", "") << "\n";
            }
            return out.str();
        }

        if (j.contains("agents")) {
            out << "角色列表:\n";
            for (const auto& a : j["agents"]) {
                out << "  • " << a.value("name", "") << "  ["
                    << a.value("id", "") << "]\n";
            }
            return out.str();
        }

        // 操作结果
        if (j.contains("agent_id") && j.contains("name")) {
            out << "角色已创建: " << j.value("name", "")
                << "  [" << j.value("agent_id", "") << "]";
            return out.str();
        }

        if (j.contains("world_id") && j.contains("name")) {
            out << "世界已创建: " << j.value("name", "")
                << "  [" << j.value("world_id", "") << "]";
            return out.str();
        }

        if (j.contains("scene_id")) {
            out << "场景: " << j.value("scene_id", "");
            return out.str();
        }

        // 通用
        return j.dump(2);
    } catch (...) {
        return json_str;
    }
}
```

在 `add_system` 方法调用处（或等效的构造路径），对包含 `{` 的内容调用 `format_worldbuilding_result`。

- [ ] **Step 2: 提交**

```bash
git add cli/src/tui/history_cell/history_cell.hpp
git commit -m "feat: render worldbuilding command results in SystemCell"
```

---

### Task 9: 构建验证

- [ ] **Step 1: 编译检查**

```bash
cd /home/icepop/Merak && make clean && make 2>&1 | head -50
```

- [ ] **Step 2: 修复编译错误**

根据编译输出修复所有错误。

- [ ] **Step 3: 运行测试**

```bash
cd /home/icepop/Merak && make test 2>&1 | tail -20
```

- [ ] **Step 4: 提交**

```bash
git add -A
git commit -m "chore: fix build errors after worldbuilding integration"
```

---

## 验证清单

- [ ] `make` 编译通过，无警告
- [ ] `make test` 全部测试通过
- [ ] `merak serve` 启动成功，日志显示 WorldbuildingService 状态
- [ ] `merak tui` 连接后 `/world list` 返回结果内联显示
- [ ] 创作调度员 Agent 发送消息时能使用世界构建工具
