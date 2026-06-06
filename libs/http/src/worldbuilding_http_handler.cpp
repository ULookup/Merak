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
    r.set_content(nlohmann::json({{"ok", false}, {"error", msg}}).dump(), "application/json");
}

} // namespace

WorldbuildingHttpHandler::WorldbuildingHttpHandler(
    std::shared_ptr<worldbuilding::WorldbuildingService> service)
    : service_(std::move(service)) {}

void WorldbuildingHttpHandler::install_routes(httplib::Server& server) {
    // World
    server.Get("/api/worldbuilding/worlds",
        [this](const auto& req, auto& res) { handle_list_worlds(req, res); });
    server.Post("/api/worldbuilding/worlds",
        [this](const auto& req, auto& res) { handle_create_world(req, res); });
    server.Delete(R"(/api/worldbuilding/worlds/([^/]+))",
        [this](const auto& req, auto& res) { handle_delete_world(req, res); });
    server.Patch(R"(/api/worldbuilding/worlds/([^/]+))",
        [this](const auto& req, auto& res) { handle_update_world(req, res); });

    // Agents
    server.Get(R"(/api/worldbuilding/([^/]+)/agents)",
        [this](const auto& req, auto& res) { handle_list_agents(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/agents)",
        [this](const auto& req, auto& res) { handle_create_agent(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/agents/([^/]+))",
        [this](const auto& req, auto& res) { handle_get_agent(req, res); });
    server.Get(R"(/api/worldbuilding/agents/([^/]+)/prompt)",
        [this](const auto& req, auto& res) { handle_load_agent_prompt(req, res); });

    // Narrative
    server.Post(R"(/api/worldbuilding/([^/]+)/scenes)",
        [this](const auto& req, auto& res) { handle_scene_new(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/scenes/([^/]+)/end)",
        [this](const auto& req, auto& res) { handle_scene_end(req, res); });

    // Time
    server.Get(R"(/api/worldbuilding/([^/]+)/time)",
        [this](const auto& req, auto& res) { handle_time_now(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/time/advance)",
        [this](const auto& req, auto& res) { handle_time_advance(req, res); });

    // Foreshadowing
    server.Get(R"(/api/worldbuilding/([^/]+)/foreshadowing)",
        [this](const auto& req, auto& res) { handle_foreshadow_list(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/foreshadowing)",
        [this](const auto& req, auto& res) { handle_foreshadow_plant(req, res); });

    // Secret
    server.Get(R"(/api/worldbuilding/([^/]+)/secrets)",
        [this](const auto& req, auto& res) { handle_secret_list(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/secrets)",
        [this](const auto& req, auto& res) { handle_secret_create(req, res); });
}

// --- World handlers ---

void WorldbuildingHttpHandler::handle_list_worlds(const httplib::Request&, httplib::Response& res) {
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
        json_response(res, {{"ok", true}, {"worlds", arr}});
    } catch (const std::exception& e) {
        error_response(res, e.what());
    }
}

void WorldbuildingHttpHandler::handle_create_world(const httplib::Request& req, httplib::Response& res) {
    try {
        auto body = nlohmann::json::parse(req.body);
        std::string name = body.value("name", "");
        std::string description = body.value("description", "");
        auto world = service_->create_world(name, description);
        json_response(res, {
            {"ok", true},
            {"world_id", world.id},
            {"name", world.name}
        }, 201);
    } catch (const std::exception& e) {
        error_response(res, e.what());
    }
}

void WorldbuildingHttpHandler::handle_delete_world(const httplib::Request&, httplib::Response& res) {
    error_response(res, "Not yet implemented", 501);
}

void WorldbuildingHttpHandler::handle_update_world(const httplib::Request& req, httplib::Response& res) {
    auto world_id = req.matches[1];
    auto body = nlohmann::json::parse(req.body);
    std::optional<std::string> name;
    std::optional<std::string> description;
    if (body.contains("name")) name = body["name"].get<std::string>();
    if (body.contains("description")) description = body["description"].get<std::string>();

    try {
        auto world = service_->update_world(world_id, name, description);
        json_response(res, {{"ok", true}, {"world_id", world.id}, {"name", world.name}, {"description", world.description}});
    } catch (const std::exception& e) {
        json_response(res, {{"error", e.what()}}, 404);
    }
}

// --- Agent handlers ---

void WorldbuildingHttpHandler::handle_list_agents(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto agents = service_->agents().list_agents(wid);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& a : agents) {
            arr.push_back({
                {"id", a.id},
                {"name", a.name},
                {"display_name", a.display_name},
                {"kind", worldbuilding::to_string(a.kind)}
            });
        }
        json_response(res, {{"ok", true}, {"agents", arr}});
    } catch (const std::exception& e) {
        error_response(res, e.what());
    }
}

void WorldbuildingHttpHandler::handle_create_agent(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto body = nlohmann::json::parse(req.body);

        worldbuilding::CharacterCard card;
        card.name = body.value("name", "");
        card.gender = body.value("gender", "");
        card.age = body.value("age", 0);
        card.race = body.value("race", "");
        card.identity = body.value("identity", "");
        card.emotional_tendency = body.value("emotional_tendency", "");
        card.speaking_style = body.value("speaking_style", "");
        card.core_desire = body.value("core_desire", "");
        card.deep_fear = body.value("deep_fear", "");
        card.daily_goal = body.value("daily_goal", "");
        card.background = body.value("background", "");
        card.knowledge_scope = body.value("knowledge_scope", "");
        card.appearance = body.value("appearance", "");
        card.version = body.value("version", 1);

        if (body.contains("core_traits") && body["core_traits"].is_array()) {
            for (const auto& t : body["core_traits"]) {
                card.core_traits.push_back(t.get<std::string>());
            }
        }
        if (body.contains("taboo_topics") && body["taboo_topics"].is_array()) {
            for (const auto& t : body["taboo_topics"]) {
                card.taboo_topics.push_back(t.get<std::string>());
            }
        }

        auto agent = service_->create_character(wid, card);
        json_response(res, {
            {"ok", true},
            {"agent_id", agent.id},
            {"name", agent.name}
        }, 201);
    } catch (const std::exception& e) {
        error_response(res, e.what());
    }
}

void WorldbuildingHttpHandler::handle_get_agent(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string agent_id = req.matches[2];
        auto agent = service_->agents().get_agent(agent_id);
        if (!agent) {
            error_response(res, "Agent not found", 404);
            return;
        }
        json_response(res, {
            {"ok", true},
            {"agent", {
                {"id", agent->id},
                {"world_id", agent->world_id},
                {"name", agent->name},
                {"display_name", agent->display_name},
                {"kind", worldbuilding::to_string(agent->kind)},
                {"created_at", agent->created_at},
                {"updated_at", agent->updated_at}
            }}
        });
    } catch (const std::exception& e) {
        error_response(res, e.what());
    }
}

// --- Narrative handlers ---

void WorldbuildingHttpHandler::handle_scene_new(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto body = nlohmann::json::parse(req.body);

        worldbuilding::Scene scene;
        scene.title = body.value("title", body.value("name", ""));
        scene.chapter_id = body.value("chapter_id", "");
        scene.world_time = body.value("world_time", "");
        scene.narrative = body.value("narrative", "");
        if (body.contains("section_id") && !body["section_id"].is_null()) {
            scene.section_id = body["section_id"].get<std::string>();
        }
        if (body.contains("location_id") && !body["location_id"].is_null()) {
            scene.location_id = body["location_id"].get<std::string>();
        }
        if (body.contains("participant_ids") && body["participant_ids"].is_array()) {
            for (const auto& p : body["participant_ids"]) {
                scene.participant_ids.push_back(p.get<std::string>());
            }
        }
        scene.status = worldbuilding::SceneStatus::Draft;

        auto created = service_->create_scene(wid, scene);
        json_response(res, {
            {"ok", true},
            {"scene_id", created.id}
        }, 201);
    } catch (const std::exception& e) {
        error_response(res, e.what());
    }
}

void WorldbuildingHttpHandler::handle_scene_end(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string sid = req.matches[2];
        auto body = nlohmann::json::parse(req.body);
        std::string markdown = body.value("final_markdown", "");

        auto wrapup = service_->end_scene(wid, sid, markdown);

        nlohmann::json diaries = nlohmann::json::array();
        for (const auto& d : wrapup.diaries_written) {
            diaries.push_back({
                {"id", d.id},
                {"agent_id", d.agent_id},
                {"scene_id", d.scene_id}
            });
        }

        nlohmann::json foreshadowing = nlohmann::json::array();
        for (const auto& f : wrapup.proposed_foreshadowing) {
            foreshadowing.push_back({
                {"id", f.id},
                {"content", f.content}
            });
        }

        json_response(res, {
            {"ok", true},
            {"diaries_written", diaries},
            {"diary_count", wrapup.diaries_written.size()},
            {"relations_updated", wrapup.relations_updated.size()},
            {"proposed_foreshadowing", foreshadowing},
            {"leak_risks", wrapup.leak_risks.size()}
        });
    } catch (const std::exception& e) {
        error_response(res, e.what());
    }
}

// --- Time handlers ---

void WorldbuildingHttpHandler::handle_time_now(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto world = service_->worlds().get_world(wid);
        if (!world) {
            error_response(res, "World not found", 404);
            return;
        }
        json_response(res, {
            {"ok", true},
            {"day", 1},
            {"period", 0},
            {"label", "\xe7\xac\xac\xe4\xb8\x80\xe6\x97\xa5\xe6\x99\xa8"}
        });
    } catch (const std::exception& e) {
        error_response(res, e.what());
    }
}

void WorldbuildingHttpHandler::handle_time_advance(const httplib::Request&, httplib::Response& res) {
    error_response(res, "Not yet implemented", 501);
}

// --- Foreshadowing handlers ---

void WorldbuildingHttpHandler::handle_foreshadow_list(const httplib::Request&, httplib::Response& res) {
    error_response(res, "Not yet implemented", 501);
}

void WorldbuildingHttpHandler::handle_foreshadow_plant(const httplib::Request&, httplib::Response& res) {
    error_response(res, "Not yet implemented", 501);
}

// --- Secret handlers ---

void WorldbuildingHttpHandler::handle_secret_list(const httplib::Request&, httplib::Response& res) {
    error_response(res, "Not yet implemented", 501);
}

void WorldbuildingHttpHandler::handle_secret_create(const httplib::Request&, httplib::Response& res) {
    error_response(res, "Not yet implemented", 501);
}

// --- Agent prompt handler ---

void WorldbuildingHttpHandler::handle_load_agent_prompt(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string agent_id = req.matches[1];
        std::string prompt = service_->load_agent_prompt(agent_id);
        json_response(res, {
            {"ok", true},
            {"agent_id", agent_id},
            {"prompt", prompt}
        });
    } catch (const std::exception& e) {
        error_response(res, e.what());
    }
}

} // namespace merak
