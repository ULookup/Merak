#include <merak/worldbuilding_http_handler.hpp>
#include <merak/runtime_service.hpp>
#include <merak/worldbuilding/world_models.hpp>
#include <merak/worldbuilding/card_access.hpp>
#include <merak/worldbuilding/pipeline_manager.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <unordered_map>

namespace merak {
namespace {

void json_response(httplib::Response& r, const nlohmann::json& body, int status = 200) {
    r.status = status;
    r.set_content(body.dump(), "application/json");
}

void error_response(httplib::Response& r, const std::string& msg, int status = 400,
                    const std::string& code = "invalid_request") {
    r.status = status;
    r.set_content(nlohmann::json({
        {"ok", false},
        {"error", {{"code", code}, {"message", msg}, {"retryable", false}}}
    }).dump(), "application/json");
}

nlohmann::json world_json(const worldbuilding::WorldMeta& w) {
    return {
        {"id", w.id},
        {"name", w.name},
        {"description", w.description},
        {"created_at", w.created_at},
        {"updated_at", w.updated_at}
    };
}

nlohmann::json agent_json(const worldbuilding::AgentRecord& a) {
    return {
        {"id", a.id},
        {"world_id", a.world_id},
        {"name", a.name},
        {"display_name", a.display_name},
        {"kind", worldbuilding::to_string(a.kind)},
        {"created_at", a.created_at},
        {"updated_at", a.updated_at}
    };
}

nlohmann::json arc_json(const worldbuilding::ArcSummary& arc) {
    return {
        {"id", arc.id},
        {"title", arc.title},
        {"purpose", arc.purpose},
        {"status", arc.status},
        {"updated_at", arc.updated_at}
    };
}

nlohmann::json chapter_json(const worldbuilding::ChapterSummary& chapter) {
    nlohmann::json json{
        {"id", chapter.id},
        {"title", chapter.title},
        {"number", chapter.number},
        {"status", chapter.status},
        {"scene_count", chapter.scene_count},
        {"updated_at", chapter.updated_at}
    };
    json["arc_id"] = chapter.arc_id.has_value() ? nlohmann::json(*chapter.arc_id) : nlohmann::json(nullptr);
    return json;
}

nlohmann::json scene_json(const worldbuilding::SceneSummary& scene) {
    return {
        {"id", scene.id},
        {"title", scene.title},
        {"chapter_id", scene.chapter_id},
        {"world_time", scene.world_time},
        {"status", scene.status},
        {"participant_ids", scene.participant_ids},
        {"updated_at", scene.updated_at}
    };
}

nlohmann::json foreshadow_json(const worldbuilding::Foreshadowing& item) {
    nlohmann::json json{
        {"id", item.id},
        {"content", item.content},
        {"pay_off_idea", item.pay_off_idea},
        {"status", worldbuilding::to_string(item.status)},
        {"hint_level", worldbuilding::to_string(item.hint_level)},
        {"tags", item.tags}
    };
    json["planted_at"] = item.planted_at.has_value() ? nlohmann::json(*item.planted_at) : nlohmann::json(nullptr);
    json["paid_at"] = item.paid_at.has_value() ? nlohmann::json(*item.paid_at) : nlohmann::json(nullptr);
    return json;
}

nlohmann::json secret_json(const worldbuilding::Secret& secret) {
    return {
        {"id", secret.id},
        {"title", secret.holder_id.empty() ? secret.public_version : secret.holder_id},
        {"truth", secret.truth},
        {"public_version", secret.public_version},
        {"stakes", secret.stakes},
        {"status", worldbuilding::to_string(secret.status)},
        {"aware_character_ids", secret.aware_character_ids},
        {"suspicious_character_ids", secret.suspicious_character_ids}
    };
}

std::optional<worldbuilding::ChapterStatus> chapter_status_from_query(const std::string& value) {
    if (value.empty()) return std::nullopt;
    if (value == "outline") return worldbuilding::ChapterStatus::Outline;
    if (value == "drafting") return worldbuilding::ChapterStatus::Drafting;
    if (value == "completed") return worldbuilding::ChapterStatus::Completed;
    if (value == "revised") return worldbuilding::ChapterStatus::Revised;
    throw std::runtime_error("invalid chapter status: " + value);
}

std::optional<worldbuilding::SceneStatus> scene_status_from_query(const std::string& value) {
    if (value.empty()) return std::nullopt;
    if (value == "draft") return worldbuilding::SceneStatus::Draft;
    if (value == "writing") return worldbuilding::SceneStatus::Writing;
    if (value == "completed") return worldbuilding::SceneStatus::Completed;
    throw std::runtime_error("invalid scene status: " + value);
}

std::optional<worldbuilding::ForeshadowStatus> foreshadow_status_from_query(const std::string& value) {
    if (value.empty()) return std::nullopt;
    if (value == "open") return worldbuilding::ForeshadowStatus::Open;
    if (value == "paid") return worldbuilding::ForeshadowStatus::Paid;
    if (value == "abandoned") return worldbuilding::ForeshadowStatus::Abandoned;
    throw std::runtime_error("invalid foreshadowing status: " + value);
}

std::optional<worldbuilding::SecretStatus> secret_status_from_query(const std::string& value) {
    if (value.empty()) return std::nullopt;
    if (value == "active") return worldbuilding::SecretStatus::Active;
    if (value == "exposed") return worldbuilding::SecretStatus::Exposed;
    if (value == "abandoned") return worldbuilding::SecretStatus::Abandoned;
    throw std::runtime_error("invalid secret status: " + value);
}

void emit_story_update(const std::shared_ptr<RuntimeService>& runtime,
                       const std::string& session_id,
                       const std::string& world_id,
                       const std::string& resource_type,
                       const std::string& resource_id) {
    if (!runtime || session_id.empty()) return;
    try {
        runtime->emit_event(session_id, "", "story_context_updated", {
            {"world_id", world_id},
            {"resource_type", resource_type},
            {"resource_id", resource_id}
        });
    } catch (...) {
    }
}

nlohmann::json image_json(const ImageRecord& img) {
    return {
        {"id", img.id},
        {"agent_id", img.agent_id},
        {"image_type", img.image_type},
        {"storage_key", img.storage_key},
        {"mime_type", img.mime_type},
        {"original_name", img.original_name},
        {"file_size_bytes", img.file_size_bytes},
        {"is_primary", img.is_primary},
        {"sort_order", img.sort_order},
        {"created_at", img.created_at}
    };
}

} // namespace

WorldbuildingHttpHandler::WorldbuildingHttpHandler(
    std::shared_ptr<worldbuilding::WorldbuildingService> service,
    std::shared_ptr<RuntimeService> runtime)
    : service_(std::move(service)), runtime_(std::move(runtime)) {}

void WorldbuildingHttpHandler::set_pipeline_manager(
    std::shared_ptr<worldbuilding::PipelineManager> mgr) {
    pipeline_mgr_ = std::move(mgr);
}

void WorldbuildingHttpHandler::set_image_service(
    std::shared_ptr<ImageService> img_svc) {
    image_service_ = std::move(img_svc);
}

void WorldbuildingHttpHandler::install_routes(httplib::Server& server) {
    // World
    server.Get("/api/worldbuilding/worlds",
        [this](const auto& req, auto& res) { handle_list_worlds(req, res); });
    server.Post("/api/worldbuilding/worlds",
        [this](const auto& req, auto& res) { handle_create_world(req, res); });
    server.Get(R"(/api/worldbuilding/worlds/([^/]+))",
        [this](const auto& req, auto& res) { handle_get_world(req, res); });
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
    server.Patch(R"(/api/worldbuilding/([^/]+)/agents/([^/]+))",
        [this](const auto& req, auto& res) { handle_patch_agent(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/diaries)",
        [this](const auto& req, auto& res) { handle_agent_diary_list(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/diaries)",
        [this](const auto& req, auto& res) { handle_agent_diary_add(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/relations)",
        [this](const auto& req, auto& res) { handle_agent_relations(req, res); });
    server.Get(R"(/api/worldbuilding/agents/([^/]+)/prompt)",
        [this](const auto& req, auto& res) { handle_load_agent_prompt(req, res); });

    // Image routes
    server.Get(R"(/api/worldbuilding/images/([^/]+))",
        [this](const auto& req, auto& res) { handle_serve_image(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/images)",
        [this](const auto& req, auto& res) { handle_list_images(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/images)",
        [this](const auto& req, auto& res) { handle_upload_image(req, res); });
    server.Delete(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/images/([^/]+))",
        [this](const auto& req, auto& res) { handle_delete_image(req, res); });
    server.Patch(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/images/([^/]+))",
        [this](const auto& req, auto& res) { handle_patch_image(req, res); });

    // Chunked upload
    server.Post(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/images/chunked)",
        [this](const auto& req, auto& res) { handle_init_chunked(req, res); });
    server.Put(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/images/chunked/([^/]+))",
        [this](const auto& req, auto& res) { handle_upload_chunk(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/images/chunked/([^/]+))",
        [this](const auto& req, auto& res) { handle_chunked_status(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/images/chunked/([^/]+)/complete)",
        [this](const auto& req, auto& res) { handle_complete_chunked(req, res); });
    server.Delete(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/images/chunked/([^/]+))",
        [this](const auto& req, auto& res) { handle_cancel_chunked(req, res); });

    // Narrative
    server.Get(R"(/api/worldbuilding/([^/]+)/overview)",
        [this](const auto& req, auto& res) { handle_overview(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/chapters)",
        [this](const auto& req, auto& res) { handle_list_chapters(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/chapters/([^/]+))",
        [this](const auto& req, auto& res) { handle_get_chapter(req, res); });
    server.Patch(R"(/api/worldbuilding/([^/]+)/chapters/([^/]+))",
        [this](const auto& req, auto& res) { handle_patch_chapter(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/chapters/([^/]+)/review)",
        [this](const auto& req, auto& res) { handle_chapter_review(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/scenes)",
        [this](const auto& req, auto& res) { handle_list_scenes(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/scenes)",
        [this](const auto& req, auto& res) { handle_scene_new(req, res); });
    server.Patch(R"(/api/worldbuilding/([^/]+)/scenes/([^/]+))",
        [this](const auto& req, auto& res) { handle_patch_scene(req, res); });
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
    server.Patch(R"(/api/worldbuilding/([^/]+)/foreshadowing/([^/]+))",
        [this](const auto& req, auto& res) { handle_patch_foreshadow(req, res); });

    // Secret
    server.Get(R"(/api/worldbuilding/([^/]+)/secrets)",
        [this](const auto& req, auto& res) { handle_secret_list(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/secrets)",
        [this](const auto& req, auto& res) { handle_secret_create(req, res); });
    server.Patch(R"(/api/worldbuilding/([^/]+)/secrets/([^/]+))",
        [this](const auto& req, auto& res) { handle_patch_secret(req, res); });

    // ─── Pipeline endpoints ───
    server.Get(R"(/api/worldbuilding/([^/]+)/pipeline/state)",
        [this](const auto& req, auto& res) {
            std::string world_id = req.matches[1];
            if (!pipeline_mgr_) {
                res.status = 503;
                res.set_content(R"({"error":"pipeline_not_available"})", "application/json");
                return;
            }
            auto data = pipeline_mgr_->get_view_data(world_id);
            const auto* wf = pipeline_mgr_->get_workflow(data.active_workflow_name);
            const auto* phase_def = wf ? wf->get_phase(data.state.current_phase) : nullptr;

            nlohmann::json response;
            response["phase"] = worldbuilding::to_string(data.state.current_phase);
            response["label"] = phase_def ? phase_def->label : "";
            response["active_workflow"] = data.active_workflow_name;

            nlohmann::json conds = nlohmann::json::array();
            for (auto& r : data.current_conditions.results) {
                nlohmann::json cj;
                cj["name"] = r.message;
                cj["met"] = r.met;
                if (r.current) cj["current"] = *r.current;
                if (r.target) cj["target"] = *r.target;
                conds.push_back(cj);
            }
            response["conditions"] = conds;
            response["all_conditions_met"] = data.current_conditions.all_met;

            auto next_phases = worldbuilding::allowed_next_phases(data.state.current_phase);
            nlohmann::json next_arr = nlohmann::json::array();
            for (auto& np : next_phases) next_arr.push_back(worldbuilding::to_string(np));
            response["next_allowed"] = next_arr;
            response["allowed_retreat"] = phase_def ? nlohmann::json(phase_def->allowed_retreat) : nlohmann::json::array();

            nlohmann::json history = nlohmann::json::array();
            for (auto& h : data.recent_history) {
                nlohmann::json hj;
                hj["id"] = h.id;
                hj["from"] = worldbuilding::to_string(h.from_phase);
                hj["to"] = worldbuilding::to_string(h.to_phase);
                hj["trigger"] = h.trigger;
                hj["timestamp"] = h.timestamp;
                history.push_back(hj);
            }
            response["recent_history"] = history;

            res.set_content(response.dump(), "application/json");
        });

    server.Post(R"(/api/worldbuilding/([^/]+)/pipeline/advance)",
        [this](const auto& req, auto& res) {
            std::string world_id = req.matches[1];
            if (!pipeline_mgr_) {
                res.status = 503;
                res.set_content(R"({"error":"pipeline_not_available"})", "application/json");
                return;
            }
            auto body = nlohmann::json::parse(req.body);

            worldbuilding::PipelineManager::AdvanceRequest areq;
            areq.world_id = world_id;
            if (body.contains("target_phase")) {
                std::string phase_str = body["target_phase"];
                areq.target_phase = worldbuilding::creative_phase_from_string(phase_str);
            }
            areq.trigger = "manual";
            areq.triggered_by = "user_click";
            areq.force = body.value("force", false);

            auto result = pipeline_mgr_->advance_phase(areq);
            if (result == worldbuilding::PipelineManager::AdvanceResult::SUCCESS) {
                res.set_content(R"({"ok":true})", "application/json");
            } else {
                res.status = 400;
                nlohmann::json err = {
                    {"ok", false},
                    {"error", worldbuilding::PipelineManager::advance_result_to_string(result)}
                };
                res.set_content(err.dump(), "application/json");
            }
        });

    server.Get("/api/worldbuilding/pipeline/workflows",
        [this](const auto& req, auto& res) {
            if (!pipeline_mgr_) {
                res.status = 503;
                res.set_content(R"({"error":"pipeline_not_available"})", "application/json");
                return;
            }
            auto names = pipeline_mgr_->list_workflows();
            nlohmann::json list = nlohmann::json::array();
            for (auto& name : names) {
                auto* wf = pipeline_mgr_->get_workflow(name);
                if (!wf) continue;
                list.push_back({
                    {"name", wf->name},
                    {"description", wf->description},
                    {"version", wf->version},
                    {"phase_count", wf->phases.size()}
                });
            }
            nlohmann::json response{{"workflows", list}};
            res.set_content(response.dump(), "application/json");
        });

    server.Get("/api/worldbuilding/pipeline/history",
        [this](const auto& req, auto& res) {
            if (!pipeline_mgr_) {
                res.status = 503;
                res.set_content(R"({"error":"pipeline_not_available"})", "application/json");
                return;
            }
            if (!req.has_param("world_id") || req.get_param_value("world_id").empty()) {
                error_response(res, "Missing required query parameter: world_id", 400, "missing_param");
                return;
            }
            std::string world_id = req.get_param_value("world_id");
            int limit = 10;
            if (req.has_param("limit")) {
                try {
                    limit = std::stoi(req.get_param_value("limit"));
                    if (limit < 1) limit = 1;
                    if (limit > 100) limit = 100;
                } catch (...) {
                    error_response(res, "Invalid limit parameter", 400);
                    return;
                }
            }
            try {
                auto records = pipeline_mgr_->load_recent_history(world_id, limit);
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& h : records) {
                    nlohmann::json hj;
                    hj["id"] = h.id;
                    hj["world_id"] = h.world_id;
                    hj["from_phase"] = worldbuilding::to_string(h.from_phase);
                    hj["to_phase"] = worldbuilding::to_string(h.to_phase);
                    hj["trigger"] = h.trigger;
                    if (h.triggered_by) hj["triggered_by"] = *h.triggered_by;
                    nlohmann::json cs;
                    cs["all_met"] = h.conditions_at_transition.all_met;
                    cs["phase"] = h.conditions_at_transition.phase_key;
                    nlohmann::json crs = nlohmann::json::array();
                    for (const auto& cr : h.conditions_at_transition.results) {
                        nlohmann::json crj;
                        crj["name"] = cr.message;
                        crj["met"] = cr.met;
                        if (cr.current) crj["current"] = *cr.current;
                        if (cr.target) crj["target"] = *cr.target;
                        crs.push_back(crj);
                    }
                    cs["results"] = crs;
                    hj["conditions_summary"] = cs;
                    hj["timestamp"] = h.timestamp;
                    arr.push_back(hj);
                }
                json_response(res, {{"ok", true}, {"history", arr}});
            } catch (const std::exception& e) {
                error_response(res, std::string("Database error: ") + e.what(), 500, "database_error");
            }
        });

    server.Post(R"(/api/worldbuilding/([^/]+)/pipeline/activate)",
        [this](const auto& req, auto& res) {
            std::string world_id = req.matches[1];
            if (!pipeline_mgr_) {
                res.status = 503;
                res.set_content(R"({"error":"pipeline_not_available"})", "application/json");
                return;
            }
            auto body = nlohmann::json::parse(req.body);
            auto workflow_name = body.value("workflow_name", "default_creative_pipeline");
            if (!pipeline_mgr_->activate_workflow(world_id, workflow_name)) {
                error_response(res, "Workflow not found: " + workflow_name, 400, "workflow_not_found");
                return;
            }
            res.set_content(R"({"ok":true})", "application/json");
        });
}

// --- World handlers ---

void WorldbuildingHttpHandler::handle_list_worlds(const httplib::Request&, httplib::Response& res) {
    try {
        auto worlds = service_->list_worlds();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& w : worlds) {
            auto wj = world_json(w);
            if (runtime_) {
                wj["active_sessions"] = runtime_->world_session_count(w.id);
            } else {
                wj["active_sessions"] = 0;
            }
            arr.push_back(wj);
        }
        json_response(res, {{"ok", true}, {"worlds", arr}});
    } catch (const std::exception& e) {
        error_response(res, e.what());
    }
}

void WorldbuildingHttpHandler::handle_get_world(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string world_id = req.matches[1];
        auto world = service_->worlds().get_world(world_id);
        if (!world) {
            error_response(res, "World not found", 404, "world_not_found");
            return;
        }
        auto foreshadow_stats = service_->foreshadowing().stats(world_id);
        auto active_secrets = service_->secrets().list(world_id, worldbuilding::SecretStatus::Active);
        auto chapters = service_->narrative().list_chapters(world_id);
        auto scenes = service_->narrative().list_scenes(world_id);
        auto agents = service_->agents().list_agents(world_id);
        auto json = world_json(*world);
        json["stats"] = {
            {"agents", agents.size()},
            {"chapters", chapters.size()},
            {"scenes", scenes.size()},
            {"open_foreshadowing", foreshadow_stats.open},
            {"active_secrets", active_secrets.size()}
        };
        json_response(res, {{"ok", true}, {"world", json}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
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
            {"world", world_json(world)},
            {"world_id", world.id},
            {"name", world.name},
            {"description", world.description}
        }, 201);
    } catch (const std::exception& e) {
        error_response(res, e.what());
    }
}

void WorldbuildingHttpHandler::handle_delete_world(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string world_id = req.matches[1];
        service_->worlds().delete_world(world_id);
        json_response(res, {{"deleted", world_id}}, 200);
    } catch (const std::exception& e) {
        error_response(res, e.what());
    }
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
        auto world = service_->worlds().get_world(wid);
        if (!world) {
            error_response(res, "World not found", 404, "world_not_found");
            return;
        }
        auto agents = service_->agents().list_agents(wid);
        // Batch-fetch primary avatars to avoid N+1 queries
        std::unordered_map<std::string, ImageRecord> primary_avatars;
        if (image_service_) {
            std::vector<std::string> agent_ids;
            agent_ids.reserve(agents.size());
            for (const auto& a : agents) agent_ids.push_back(a.id);
            primary_avatars = image_service_->list_primary_avatars(agent_ids);
        }
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& a : agents) {
            nlohmann::json agent_obj = {
                {"id", a.id},
                {"name", a.name},
                {"display_name", a.display_name},
                {"kind", worldbuilding::to_string(a.kind)}
            };
            auto it = primary_avatars.find(a.id);
            if (it != primary_avatars.end()) {
                agent_obj["avatar_url"] = "/api/worldbuilding/images/" + it->second.id;
            }
            arr.push_back(agent_obj);
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
        emit_story_update(runtime_, body.value("session_id", ""), wid, "agent", agent.id);
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
        std::string wid = req.matches[1];
        std::string agent_id = req.matches[2];
        auto agent = service_->agents().get_agent(agent_id);
        if (!agent) {
            error_response(res, "Agent not found", 404, "agent_not_found");
            return;
        }
        if (agent->world_id != wid) {
            error_response(res, "Agent not found in this world", 404, "agent_not_found");
            return;
        }
        auto card = service_->agents().load_character_card(agent_id);
        auto j = agent_json(*agent);
        j["character_card"] = {
            {"version", card.version},
            {"age", card.age},
            {"gender", card.gender},
            {"race", card.race},
            {"identity", card.identity},
            {"core_traits", card.core_traits},
            {"emotional_tendency", card.emotional_tendency},
            {"speaking_style", card.speaking_style},
            {"core_desire", card.core_desire},
            {"deep_fear", card.deep_fear},
            {"daily_goal", card.daily_goal},
            {"background", card.background},
            {"knowledge_scope", card.knowledge_scope},
            {"appearance", card.appearance},
            {"taboo_topics", card.taboo_topics}
        };
        if (image_service_) {
            auto imgs = image_service_->list_images(agent_id);
            nlohmann::json avatars = nlohmann::json::array();
            nlohmann::json designs = nlohmann::json::array();
            for (const auto& img : imgs) {
                auto img_j = image_json(img);
                img_j["url"] = "/api/worldbuilding/images/" + img.id;
                if (img.image_type == "avatar") {
                    avatars.push_back(img_j);
                    if (img.is_primary) {
                        j["avatar_url"] = "/api/worldbuilding/images/" + img.id;
                    }
                } else if (img.image_type == "design") {
                    designs.push_back(img_j);
                }
            }
            j["images"] = {{"avatar", avatars}, {"design", designs}};
        }
        json_response(res, {{"ok", true}, {"agent", j}});
    } catch (const std::exception& e) {
        error_response(res, e.what());
    }
}

// --- Narrative handlers ---

void WorldbuildingHttpHandler::handle_overview(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        if (!service_->worlds().get_world(wid)) {
            error_response(res, "World not found", 404, "world_not_found");
            return;
        }

        auto arcs = service_->narrative().list_arcs(wid);
        auto chapters = service_->narrative().list_chapters(wid);
        auto agents = service_->agents().list_agents(wid);
        auto foreshadows = service_->foreshadowing().list(wid, worldbuilding::ForeshadowStatus::Open);
        auto secrets = service_->secrets().list(wid, worldbuilding::SecretStatus::Active);

        std::optional<worldbuilding::ChapterSummary> current_chapter;
        for (const auto& chapter : chapters) {
            if (chapter.status != "completed" && chapter.status != "revised") {
                current_chapter = chapter;
            }
        }
        if (!current_chapter.has_value() && !chapters.empty()) current_chapter = chapters.back();

        std::optional<worldbuilding::SceneSummary> current_scene;
        if (current_chapter.has_value()) {
            auto chapter_scenes = service_->narrative().list_scenes(wid, current_chapter->id);
            for (const auto& scene : chapter_scenes) {
                if (scene.status == "writing" || scene.status == "draft") {
                    current_scene = scene;
                    break;
                }
            }
            if (!current_scene.has_value() && !chapter_scenes.empty()) current_scene = chapter_scenes.front();
        }

        nlohmann::json agent_arr = nlohmann::json::array();
        for (const auto& agent : agents) agent_arr.push_back(agent_json(agent));
        nlohmann::json foreshadow_arr = nlohmann::json::array();
        for (const auto& item : foreshadows) foreshadow_arr.push_back(foreshadow_json(item));
        nlohmann::json secret_arr = nlohmann::json::array();
        for (const auto& secret : secrets) secret_arr.push_back(secret_json(secret));

        nlohmann::json response{
            {"ok", true},
            {"agents", agent_arr},
            {"foreshadowing", foreshadow_arr},
            {"secrets", secret_arr},
            {"world_time", {{"day", 1}, {"period", 0}, {"label", "Day 1 Dawn"}}}
        };
        if (current_chapter.has_value()) {
            response["current_chapter"] = chapter_json(*current_chapter);
            if (current_chapter->arc_id.has_value()) {
                auto it = std::find_if(arcs.begin(), arcs.end(), [&](const auto& arc) {
                    return arc.id == *current_chapter->arc_id;
                });
                if (it != arcs.end()) response["current_arc"] = arc_json(*it);
            }
        }
        if (current_scene.has_value()) response["current_scene"] = scene_json(*current_scene);

        json_response(res, response);
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_list_chapters(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto status = chapter_status_from_query(req.has_param("status") ? req.get_param_value("status") : "");
        auto chapters = service_->narrative().list_chapters(wid, status);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& chapter : chapters) arr.push_back(chapter_json(chapter));
        json_response(res, {{"ok", true}, {"chapters", arr}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400, "invalid_request");
    }
}

void WorldbuildingHttpHandler::handle_get_chapter(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string cid = req.matches[2];

        if (!service_->worlds().get_world(wid)) {
            error_response(res, "World not found", 404, "world_not_found");
            return;
        }

        auto chapter = service_->narrative().get_chapter(wid, cid);
        if (!chapter) {
            error_response(res, "Chapter not found", 404, "chapter_not_found");
            return;
        }

        nlohmann::json response{
            {"ok", true},
            {"id", chapter->id},
            {"title", chapter->title},
            {"number", chapter->number},
            {"status", worldbuilding::to_string(chapter->status)},
            {"content", chapter->content},
            {"pitch", chapter->pitch},
            {"notes", chapter->notes},
            {"scene_ids", chapter->scene_ids},
            {"foreshadowing_planted", chapter->foreshadowing_planted},
            {"foreshadowing_paid", chapter->foreshadowing_paid}
        };
        response["arc_id"] = chapter->arc_id.has_value()
            ? nlohmann::json(*chapter->arc_id)
            : nlohmann::json(nullptr);

        json_response(res, response);
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_list_scenes(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::optional<std::string> chapter_id;
        if (req.has_param("chapter_id") && !req.get_param_value("chapter_id").empty()) {
            chapter_id = req.get_param_value("chapter_id");
        }
        auto status = scene_status_from_query(req.has_param("status") ? req.get_param_value("status") : "");
        auto scenes = service_->narrative().list_scenes(wid, chapter_id, status);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& scene : scenes) arr.push_back(scene_json(scene));
        json_response(res, {{"ok", true}, {"scenes", arr}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400, "invalid_request");
    }
}

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
        emit_story_update(runtime_, body.value("session_id", ""), wid, "scene", created.id);
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
        emit_story_update(runtime_, body.value("session_id", ""), wid, "scene", sid);

        auto scene = service_->narrative().get_scene(wid, sid);
        std::string world_time = scene ? scene->world_time : "";

        nlohmann::json pending = nlohmann::json::array();
        for (const auto& agent_id : wrapup.pending_diary_agents) {
            pending.push_back(agent_id);
        }

        nlohmann::json foreshadowing = nlohmann::json::array();
        for (const auto& f : wrapup.proposed_foreshadowing) {
            foreshadowing.push_back({
                {"id", f.id},
                {"content", f.content}
            });
        }

        json_response(res, nlohmann::json{
            {"scene_id", sid},
            {"status", "completed"},
            {"world_time", world_time},
            {"relations_updated", wrapup.relations_updated.size()},
            {"foreshadowing_proposed", foreshadowing},
            {"pending_diary_agents", pending},
            {"pending_diary_count", wrapup.pending_diary_agents.size()}
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

void WorldbuildingHttpHandler::handle_time_advance(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string world_id = req.matches[1];
        auto body = nlohmann::json::parse(req.body);
        std::string new_time = body.at("world_time");

        // Validate world exists
        auto world = service_->worlds().get_world(world_id);
        if (!world) {
            error_response(res, "World not found", 404, "world_not_found");
            return;
        }

        // Parse and validate the new time format
        auto parsed_new = worldbuilding::WorldTime::parse(new_time);
        if (!parsed_new.has_value()) {
            error_response(res, "Invalid world_time format: " + new_time, 400, "invalid_time_format");
            return;
        }

        // Forward-only validation: new_time must be strictly after current world time.
        // The current world time is the world_time of the most recent timeline event.
        auto world_path = service_->worlds().world_path(world_id);
        auto timeline_path = world_path / "timeline.json";
        if (std::filesystem::exists(timeline_path)) {
            std::ifstream input(timeline_path);
            if (input) {
                auto timeline = nlohmann::json::parse(input);
                if (timeline.contains("events") && timeline["events"].is_array() &&
                    !timeline["events"].empty()) {
                    const auto& events = timeline["events"];
                    const auto& last_event = events.back();
                    std::string current_time_str = last_event.at("world_time").get<std::string>();
                    auto parsed_current = worldbuilding::WorldTime::parse(current_time_str);
                    if (parsed_current.has_value() && *parsed_new <= *parsed_current) {
                        error_response(res,
                            "Time can only advance forward. Current world time: " +
                                current_time_str + ", requested: " + new_time,
                            400, "time_not_forward");
                        return;
                    }
                }
            }
        }

        worldbuilding::TimelineEvent event;
        event.world_time = new_time;
        event.description = body.value("description", "Time advanced");
        event.recorded_by = body.value("recorded_by", "user");

        auto recorded = service_->narrative().advance_time(world_id, std::move(event));

        json_response(res, {
            {"ok", true},
            {"world_id", world_id},
            {"world_time", recorded.world_time},
            {"event_id", recorded.id},
            {"description", recorded.description}
        });
    } catch (const std::exception& e) {
        error_response(res, e.what());
    }
}

// --- Foreshadowing handlers ---

void WorldbuildingHttpHandler::handle_foreshadow_list(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto status = foreshadow_status_from_query(req.has_param("status") ? req.get_param_value("status") : "");
        auto items = service_->foreshadowing().list(wid, status);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& item : items) arr.push_back(foreshadow_json(item));
        json_response(res, {{"ok", true}, {"items", arr}, {"foreshadowing", arr}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400, "invalid_request");
    }
}

void WorldbuildingHttpHandler::handle_foreshadow_plant(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto body = nlohmann::json::parse(req.body);
        worldbuilding::Foreshadowing item;
        item.content = body.value("content", body.value("hint", ""));
        item.pay_off_idea = body.value("pay_off_idea", "");
        item.hint_level = body.value("hint_level", "visible") == "subtle"
            ? worldbuilding::ForeshadowHintLevel::Subtle
            : body.value("hint_level", "visible") == "obvious"
                ? worldbuilding::ForeshadowHintLevel::Obvious
                : worldbuilding::ForeshadowHintLevel::Visible;
        if (body.contains("tags") && body["tags"].is_array()) {
            for (const auto& tag : body["tags"]) item.tags.push_back(tag.get<std::string>());
        }
        auto created = service_->plant_foreshadowing(wid, item);
        emit_story_update(runtime_, body.value("session_id", ""), wid, "foreshadowing", created.id);
        json_response(res, {{"ok", true}, {"item", foreshadow_json(created)}, {"foreshadowing_id", created.id}}, 201);
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Secret handlers ---

void WorldbuildingHttpHandler::handle_secret_list(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto status = secret_status_from_query(req.has_param("status") ? req.get_param_value("status") : "");
        auto items = service_->secrets().list(wid, status);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& item : items) arr.push_back(secret_json(item));
        json_response(res, {{"ok", true}, {"items", arr}, {"secrets", arr}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400, "invalid_request");
    }
}

void WorldbuildingHttpHandler::handle_secret_create(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto body = nlohmann::json::parse(req.body);
        worldbuilding::Secret secret;
        secret.holder_id = body.value("holder_id", body.value("title", ""));
        secret.truth = body.value("truth", "");
        secret.public_version = body.value("public_version", "");
        secret.stakes = body.value("stakes", "");
        if (body.contains("aware_character_ids") && body["aware_character_ids"].is_array()) {
            for (const auto& id : body["aware_character_ids"]) secret.aware_character_ids.push_back(id.get<std::string>());
        }
        if (body.contains("suspicious_character_ids") && body["suspicious_character_ids"].is_array()) {
            for (const auto& id : body["suspicious_character_ids"]) secret.suspicious_character_ids.push_back(id.get<std::string>());
        }
        if (body.contains("related_foreshadowing_ids") && body["related_foreshadowing_ids"].is_array()) {
            for (const auto& id : body["related_foreshadowing_ids"]) secret.related_foreshadowing_ids.push_back(id.get<std::string>());
        }
        auto created = service_->create_secret(wid, secret);
        emit_story_update(runtime_, body.value("session_id", ""), wid, "secret", created.id);
        json_response(res, {{"ok", true}, {"item", secret_json(created)}, {"secret_id", created.id}}, 201);
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
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

// --- Agent card PATCH ---

void WorldbuildingHttpHandler::handle_patch_agent(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    std::string aid = req.matches[2];
    try {
        auto agent = service_->agents().get_agent(aid);
        if (!agent) {
            error_response(res, "Agent not found", 404, "agent_not_found");
            return;
        }
        if (agent->world_id != wid) {
            error_response(res, "Agent not found in this world", 404, "agent_not_found");
            return;
        }
        auto body = nlohmann::json::parse(req.body);
        auto fields = body.at("fields");
        int version = body.value("version", 0);
        auto card = service_->agents().patch_character_card(aid, fields, version);
        json_response(res, {{"ok", true}, {"version", card.version}});
    } catch (const worldbuilding::VersionConflictError& e) {
        nlohmann::json j = {
            {"ok", false},
            {"error", {
                {"code", "version_conflict"},
                {"message", "卡片已被其他来源修改，请刷新后重试"},
                {"current_version", e.current_version},
                {"retryable", true}
            }}
        };
        res.status = 409;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Agent diary handlers ---

void WorldbuildingHttpHandler::handle_agent_diary_list(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string aid = req.matches[2];
        auto agent = service_->agents().get_agent(aid);
        if (!agent) {
            error_response(res, "Agent not found", 404, "agent_not_found");
            return;
        }
        if (agent->world_id != wid) {
            error_response(res, "Agent not found in this world", 404, "agent_not_found");
            return;
        }
        auto diaries = service_->agents().recent_diary(aid, 50);
        nlohmann::json arr = nlohmann::json::array();
        for (auto& d : diaries) {
            arr.push_back({
                {"id", d.id},
                {"agent_id", d.agent_id},
                {"scene_id", d.scene_id},
                {"content", d.content},
                {"world_time", d.world_time},
                {"created_at", d.created_at}
            });
        }
        json_response(res, {{"ok", true}, {"diaries", arr}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_agent_diary_add(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    std::string aid = req.matches[2];
    try {
        auto agent = service_->agents().get_agent(aid);
        if (!agent) {
            error_response(res, "Agent not found", 404, "agent_not_found");
            return;
        }
        if (agent->world_id != wid) {
            error_response(res, "Agent not found in this world", 404, "agent_not_found");
            return;
        }
        auto body = nlohmann::json::parse(req.body);
        worldbuilding::DiaryEntry entry;
        entry.agent_id = aid;
        entry.scene_id = body.value("scene_id", "");
        entry.content = body.at("content").get<std::string>();
        entry.world_time = body.value("world_time", "");
        service_->agents().append_diary_entry(std::move(entry));
        json_response(res, {{"ok", true}}, 201);
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Agent relations ---

void WorldbuildingHttpHandler::handle_agent_relations(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string aid = req.matches[2];
        auto agent = service_->agents().get_agent(aid);
        if (!agent) {
            error_response(res, "Agent not found", 404, "agent_not_found");
            return;
        }
        if (agent->world_id != wid) {
            error_response(res, "Agent not found in this world", 404, "agent_not_found");
            return;
        }
        auto relations = service_->agents().relations_for(aid);
        nlohmann::json arr = nlohmann::json::array();
        for (auto& rel : relations) {
            arr.push_back({
                {"agent_id", rel.agent_id},
                {"target_id", rel.target_id},
                {"relation_type", rel.relation_type},
                {"description", rel.description},
                {"intimacy", rel.intimacy},
                {"key_events", rel.key_events},
                {"updated_at", rel.updated_at}
            });
        }
        json_response(res, {{"ok", true}, {"relations", arr}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- PATCH scene / chapter ---

void WorldbuildingHttpHandler::handle_patch_scene(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    std::string sid = req.matches[2];
    try {
        auto scene = service_->narrative().get_scene(wid, sid);
        if (!scene) {
            error_response(res, "Scene not found", 404, "scene_not_found");
            return;
        }
        auto body = nlohmann::json::parse(req.body);
        auto fields = body.at("fields");
        if (fields.contains("status")) {
            auto parsed = worldbuilding::parse_scene_status(fields["status"].get<std::string>());
            if (!parsed) {
                error_response(res, "Invalid scene status", 400);
                return;
            }
        }
        service_->narrative().patch_scene(wid, sid, fields);
        json_response(res, {{"ok", true}});
    } catch (const worldbuilding::VersionConflictError& e) {
        nlohmann::json j = {
            {"ok", false},
            {"error", {
                {"code", "version_conflict"},
                {"message", "资源已被其他来源修改，请刷新后重试"},
                {"current_version", e.current_version},
                {"retryable", true}
            }}
        };
        res.status = 409;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_patch_chapter(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    std::string cid = req.matches[2];
    try {
        auto chapter = service_->narrative().get_chapter(wid, cid);
        if (!chapter) {
            error_response(res, "Chapter not found", 404, "chapter_not_found");
            return;
        }
        auto body = nlohmann::json::parse(req.body);
        auto fields = body.at("fields");
        if (fields.contains("status")) {
            auto parsed = worldbuilding::parse_chapter_status(fields["status"].get<std::string>());
            if (!parsed) {
                error_response(res, "Invalid chapter status", 400);
                return;
            }
        }
        service_->narrative().patch_chapter(wid, cid, fields);
        json_response(res, {{"ok", true}});
    } catch (const worldbuilding::VersionConflictError& e) {
        nlohmann::json j = {
            {"ok", false},
            {"error", {
                {"code", "version_conflict"},
                {"message", "资源已被其他来源修改，请刷新后重试"},
                {"current_version", e.current_version},
                {"retryable", true}
            }}
        };
        res.status = 409;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Chapter review ---

void WorldbuildingHttpHandler::handle_chapter_review(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string cid = req.matches[2];

        auto review = service_->get_chapter_review(wid, cid);

        nlohmann::json planted_arr = nlohmann::json::array();
        for (const auto& f : review.foreshadowing_planted) {
            planted_arr.push_back({{"id", f.id}, {"content", f.content}});
        }
        nlohmann::json paid_arr = nlohmann::json::array();
        for (const auto& f : review.foreshadowing_paid) {
            paid_arr.push_back({{"id", f.id}, {"content", f.content}});
        }

        json_response(res, {
            {"ok", true},
            {"review", {
                {"chapter_id", review.chapter_id},
                {"title", review.title},
                {"word_count", review.word_count},
                {"character_names", review.character_names},
                {"foreshadowing_planted", planted_arr},
                {"foreshadowing_paid", paid_arr},
                {"writing_advice", review.writing_advice}
            }}
        });
    } catch (const std::exception& e) {
        error_response(res, e.what(), 500, "review_failed");
    }
}

// --- PATCH foreshadow / secret ---

void WorldbuildingHttpHandler::handle_patch_foreshadow(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    std::string fid = req.matches[2];
    try {
        auto body = nlohmann::json::parse(req.body);
        auto fields = body.at("fields");
        if (fields.contains("status")) {
            std::string s = fields["status"].get<std::string>();
            if (s != "open" && s != "paid" && s != "abandoned") {
                error_response(res, "Invalid foreshadow status: " + s, 400);
                return;
            }
        }
        bool ok = service_->foreshadowing().patch(wid, fid, fields);
        if (!ok) {
            error_response(res, "Foreshadow not found", 404, "foreshadow_not_found");
            return;
        }
        json_response(res, {{"ok", true}});
    } catch (const worldbuilding::VersionConflictError& e) {
        nlohmann::json j = {
            {"ok", false},
            {"error", {
                {"code", "version_conflict"},
                {"message", "资源已被其他来源修改，请刷新后重试"},
                {"current_version", e.current_version},
                {"retryable", true}
            }}
        };
        res.status = 409;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_patch_secret(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    std::string sid = req.matches[2];
    try {
        auto body = nlohmann::json::parse(req.body);
        auto fields = body.at("fields");
        if (fields.contains("status")) {
            std::string s = fields["status"].get<std::string>();
            if (s != "active" && s != "revealed" && s != "abandoned") {
                error_response(res, "Invalid secret status: " + s, 400);
                return;
            }
        }
        bool ok = service_->secrets().patch(wid, sid, fields);
        if (!ok) {
            error_response(res, "Secret not found", 404, "secret_not_found");
            return;
        }
        json_response(res, {{"ok", true}});
    } catch (const worldbuilding::VersionConflictError& e) {
        nlohmann::json j = {
            {"ok", false},
            {"error", {
                {"code", "version_conflict"},
                {"message", "资源已被其他来源修改，请刷新后重试"},
                {"current_version", e.current_version},
                {"retryable", true}
            }}
        };
        res.status = 409;
        res.set_content(j.dump(), "application/json");
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Image handlers ---

void WorldbuildingHttpHandler::handle_list_images(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string aid = req.matches[2];
        if (!image_service_) {
            res.status = 503;
            res.set_content(R"({"error":"image_service_not_available"})", "application/json");
            return;
        }
        auto images = image_service_->list_images(aid);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& img : images) {
            auto j = image_json(img);
            j["url"] = "/api/worldbuilding/images/" + img.id;
            arr.push_back(j);
        }
        json_response(res, {{"ok", true}, {"images", arr}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_upload_image(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string aid = req.matches[2];
        if (!image_service_) {
            res.status = 503;
            res.set_content(R"({"error":"image_service_not_available"})", "application/json");
            return;
        }

        auto file = req.form.get_file("file");
        if (file.content.empty()) {
            error_response(res, "No file uploaded", 400, "missing_file");
            return;
        }

        std::string image_type;
        auto ft = req.form.get_file("image_type");
        if (!ft.content.empty()) {
            image_type = ft.content;
        } else if (req.has_param("image_type")) {
            image_type = req.get_param_value("image_type");
        }
        if (image_type != "avatar" && image_type != "design") {
            error_response(res, "Invalid image_type. Must be 'avatar' or 'design'", 400, "invalid_image_type");
            return;
        }

        if (file.content.size() > 10 * 1024 * 1024) {
            error_response(res, "File size exceeds 10MB limit", 400, "file_too_large");
            return;
        }

        std::vector<unsigned char> bytes(file.content.begin(), file.content.end());
        auto img = image_service_->upload(wid, aid, image_type, file.filename, file.content_type, bytes);

        auto j = image_json(img);
        j["url"] = "/api/worldbuilding/images/" + img.id;
        json_response(res, nlohmann::json{{"ok", true}, {"image", j}}, 201);
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_delete_image(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string img_id = req.matches[3];
        if (!image_service_) {
            res.status = 503;
            res.set_content(R"({"error":"image_service_not_available"})", "application/json");
            return;
        }
        image_service_->delete_image(img_id);
        json_response(res, {{"ok", true}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_patch_image(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string img_id = req.matches[3];
        if (!image_service_) {
            res.status = 503;
            res.set_content(R"({"error":"image_service_not_available"})", "application/json");
            return;
        }
        auto body = nlohmann::json::parse(req.body);
        std::optional<bool> is_primary;
        std::optional<int> sort_order;
        if (body.contains("is_primary")) is_primary = body["is_primary"].get<bool>();
        if (body.contains("sort_order")) sort_order = body["sort_order"].get<int>();
        image_service_->update_image(img_id, is_primary, sort_order);
        json_response(res, {{"ok", true}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_serve_image(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string img_id = req.matches[1];
        if (!image_service_) {
            res.status = 503;
            res.set_content(R"({"error":"image_service_not_available"})", "application/json");
            return;
        }
        auto rec = image_service_->get_image(img_id);
        if (!rec) {
            error_response(res, "Image not found", 404, "image_not_found");
            return;
        }
        auto img_data = image_service_->load_image_data(rec->storage_key);
        res.set_content(
            std::string(reinterpret_cast<const char*>(img_data.bytes.data()), img_data.bytes.size()),
            img_data.mime_type);
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Chunked upload handlers ---

void WorldbuildingHttpHandler::handle_init_chunked(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string aid = req.matches[2];
        if (!image_service_) {
            res.status = 503;
            res.set_content(R"({"error":"image_service_not_available"})", "application/json");
            return;
        }
        auto body = nlohmann::json::parse(req.body);
        std::string image_type = body.at("image_type").get<std::string>();
        if (image_type != "avatar" && image_type != "design") {
            error_response(res, "Invalid image_type. Must be 'avatar' or 'design'", 400, "invalid_image_type");
            return;
        }
        std::string file_name = body.at("file_name").get<std::string>();
        std::string mime_type = body.at("mime_type").get<std::string>();
        int64_t total_size = body.at("total_size").get<int64_t>();
        int64_t chunk_size = body.value("chunk_size", int64_t(5 * 1024 * 1024));

        auto state = image_service_->init_chunked(wid, aid, image_type, file_name, mime_type, total_size, chunk_size);

        json_response(res, {
            {"ok", true},
            {"upload_id", state.upload_id},
            {"chunks_total", state.chunks_total},
            {"chunk_size", state.chunk_size}
        }, 201);
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_upload_chunk(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string upload_id = req.matches[3];
        if (!image_service_) {
            res.status = 503;
            res.set_content(R"({"error":"image_service_not_available"})", "application/json");
            return;
        }
        if (!req.has_param("chunk_idx")) {
            error_response(res, "Missing required query parameter: chunk_idx", 400, "missing_param");
            return;
        }
        int chunk_idx = std::stoi(req.get_param_value("chunk_idx"));
        std::vector<unsigned char> data(req.body.begin(), req.body.end());
        image_service_->upload_chunk(upload_id, chunk_idx, data);

        auto uploaded = image_service_->uploaded_chunks(upload_id);
        nlohmann::json arr = nlohmann::json::array();
        for (auto idx : uploaded) arr.push_back(idx);
        json_response(res, {{"ok", true}, {"uploaded_chunks", arr}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_chunked_status(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string upload_id = req.matches[3];
        if (!image_service_) {
            res.status = 503;
            res.set_content(R"({"error":"image_service_not_available"})", "application/json");
            return;
        }
        auto uploaded = image_service_->uploaded_chunks(upload_id);
        nlohmann::json arr = nlohmann::json::array();
        for (auto idx : uploaded) arr.push_back(idx);
        json_response(res, {{"ok", true}, {"uploaded_chunks", arr}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_complete_chunked(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string upload_id = req.matches[3];
        if (!image_service_) {
            res.status = 503;
            res.set_content(R"({"error":"image_service_not_available"})", "application/json");
            return;
        }
        auto img = image_service_->complete_chunked(upload_id);
        auto j = image_json(img);
        j["url"] = "/api/worldbuilding/images/" + img.id;
        json_response(res, nlohmann::json{{"ok", true}, {"image", j}}, 201);
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_cancel_chunked(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string upload_id = req.matches[3];
        if (!image_service_) {
            res.status = 503;
            res.set_content(R"({"error":"image_service_not_available"})", "application/json");
            return;
        }
        image_service_->cancel_chunked(upload_id);
        json_response(res, {{"ok", true}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

} // namespace merak
