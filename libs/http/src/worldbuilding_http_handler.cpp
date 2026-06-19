#include <merak/worldbuilding_http_handler.hpp>
#include <merak/kg/kg_models.hpp>
#include <merak/runtime_service.hpp>
#include <merak/worldbuilding/world_models.hpp>
#include <merak/worldbuilding/card_access.hpp>
#include <merak/worldbuilding/pipeline_manager.hpp>
#include <merak/worldbuilding/ids.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <map>
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

void WorldbuildingHttpHandler::start_agent_run(
    const httplib::Request& req, httplib::Response& res,
    const std::string& world_id, const std::string& task_description,
    const std::string& operation_type) {
    if (!runtime_) {
        error_response(res, "Runtime service not available", 503, "runtime_unavailable");
        return;
    }
    try {
        auto session = runtime_->create_session("", world_id, "god_agent");
        runtime_->set_session_ephemeral(session.id, 30);
        auto run = runtime_->start_run(session.id, task_description, "");

        {
            std::lock_guard<std::mutex> lock(pending_runs_mutex_);
            pending_agent_runs_[run.id] = {world_id, operation_type};
        }
        poller_started_cv_.notify_one();

        json_response(res, {
            {"ok", true},
            {"session_id", session.id},
            {"run_id", run.id}
        }, 202);
    } catch (const RuntimeError& e) {
        int status = e.code() == "session_busy" ? 409 : 400;
        error_response(res, e.what(), status, e.code());
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::capture_agent_result(const std::string& run_id) {
    PendingAgentRun pending;
    {
        std::lock_guard<std::mutex> lock(pending_runs_mutex_);
        auto it = pending_agent_runs_.find(run_id);
        if (it == pending_agent_runs_.end()) return;
        pending = it->second;
        pending_agent_runs_.erase(it);
    }
    try {
        auto run = runtime_->get_run(run_id);
        if (!run || run->status != RunStatus::Completed) return;

        nlohmann::json messages = nlohmann::json::array();
        for (const auto& e : runtime_->events_after(run->session_id, 0)) {
            if (e.run_id == run_id && e.type == "message_appended") {
                messages.push_back({
                    {"role", e.payload.value("role", "")},
                    {"content", e.payload.value("content", "")}
                });
            }
        }
        service_->worlds().store_agent_result(pending.world_id,
                                               pending.operation_type, messages);
    } catch (...) {
        // Best-effort caching; don't crash
    }
}

void WorldbuildingHttpHandler::start_result_poller() {
    {
        std::lock_guard<std::mutex> lock(poller_started_mutex_);
        if (poller_started_) return;
        poller_started_ = true;
    }
    poller_stop_ = false;
    result_poller_ = std::thread([this] {
        while (!poller_stop_) {
            std::vector<std::string> pending_ids;
            {
                std::lock_guard<std::mutex> lock(pending_runs_mutex_);
                for (const auto& kv : pending_agent_runs_) {
                    pending_ids.push_back(kv.first);
                }
            }
            for (const auto& id : pending_ids) {
                if (poller_stop_) break;
                auto run = runtime_->get_run(id);
                if (!run) {
                    std::lock_guard<std::mutex> lock(pending_runs_mutex_);
                    pending_agent_runs_.erase(id);
                    continue;
                }
                switch (run->status) {
                case RunStatus::Completed:
                    capture_agent_result(id);
                    break;
                case RunStatus::Failed:
                case RunStatus::Cancelled:
                case RunStatus::Interrupted:
                    {
                        std::lock_guard<std::mutex> lock(pending_runs_mutex_);
                        pending_agent_runs_.erase(id);
                    }
                    break;
                default:
                    break;
                }
            }
            std::unique_lock<std::mutex> cv_lock(poller_started_mutex_);
            poller_started_cv_.wait_for(cv_lock, std::chrono::seconds(2),
                                        [this] { return poller_stop_.load(); });
        }
    });
}

void WorldbuildingHttpHandler::stop_result_poller() {
    poller_stop_ = true;
    poller_started_cv_.notify_all();
    if (result_poller_.joinable()) {
        result_poller_.join();
    }
}

WorldbuildingHttpHandler::WorldbuildingHttpHandler(
    std::shared_ptr<worldbuilding::WorldbuildingService> service,
    std::shared_ptr<RuntimeService> runtime)
    : service_(std::move(service)), runtime_(std::move(runtime)) {
    if (runtime_) start_result_poller();
}

WorldbuildingHttpHandler::~WorldbuildingHttpHandler() {
    stop_result_poller();
}

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
    server.Get(R"(/api/worldbuilding/([^/]+)/agents/search)",
        [this](const auto& req, auto& res) { handle_search_agents(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/agents)",
        [this](const auto& req, auto& res) { handle_list_agents(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/agents)",
        [this](const auto& req, auto& res) { handle_create_agent(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/agents/([^/]+))",
        [this](const auto& req, auto& res) { handle_get_agent(req, res); });
    server.Patch(R"(/api/worldbuilding/([^/]+)/agents/([^/]+))",
        [this](const auto& req, auto& res) { handle_patch_agent(req, res); });
    server.Delete(R"(/api/worldbuilding/([^/]+)/agents/([^/]+))",
        [this](const auto& req, auto& res) { handle_delete_agent(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/diaries)",
        [this](const auto& req, auto& res) { handle_agent_diary_list(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/diaries)",
        [this](const auto& req, auto& res) { handle_agent_diary_add(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/diaries/search)",
        [this](const auto& req, auto& res) { handle_agent_diary_search(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/diaries/([^/]+))",
        [this](const auto& req, auto& res) { handle_agent_diary_get(req, res); });
    server.Patch(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/diaries/([^/]+))",
        [this](const auto& req, auto& res) { handle_agent_diary_patch(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/relations)",
        [this](const auto& req, auto& res) { handle_agent_relations(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/relations)",
        [this](const auto& req, auto& res) { handle_agent_relation_upsert(req, res); });
    server.Patch(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/relations/([^/]+))",
        [this](const auto& req, auto& res) { handle_agent_relation_update(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/voice)",
        [this](const auto& req, auto& res) { handle_agent_voice(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/memory-summaries)",
        [this](const auto& req, auto& res) { handle_memory_summaries_list(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/agents/([^/]+)/memory-summaries/([^/]+))",
        [this](const auto& req, auto& res) { handle_memory_summary_get(req, res); });
    server.Get(R"(/api/worldbuilding/agents/([^/]+)/prompt)",
        [this](const auto& req, auto& res) { handle_load_agent_prompt(req, res); });

    // Character appearances
    server.Get(R"(/api/worldbuilding/([^/]+)/characters/([^/]+)/appearances)",
        [this](const auto& req, auto& res) { handle_character_appearances(req, res); });

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
    server.Get(R"(/api/worldbuilding/([^/]+)/tree)",
        [this](const auto& req, auto& res) { handle_chapter_tree(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/chapters)",
        [this](const auto& req, auto& res) { handle_list_chapters(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/chapters/([^/]+))",
        [this](const auto& req, auto& res) { handle_get_chapter(req, res); });
    server.Patch(R"(/api/worldbuilding/([^/]+)/chapters/([^/]+))",
        [this](const auto& req, auto& res) { handle_patch_chapter(req, res); });
    server.Delete(R"(/api/worldbuilding/([^/]+)/chapters/([^/]+))",
        [this](const auto& req, auto& res) { handle_delete_chapter(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/chapters/reorder)",
        [this](const auto& req, auto& res) { handle_reorder_chapters(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/chapters/([^/]+)/review)",
        [this](const auto& req, auto& res) { handle_chapter_review(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/export)",
        [this](const auto& req, auto& res) { handle_export_chapters(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/export-full)",
        [this](const auto& req, auto& res) { handle_export_full(req, res); });
    server.Post("/api/worldbuilding/import",
        [this](const auto& req, auto& res) { handle_import_snapshot(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/scenes)",
        [this](const auto& req, auto& res) { handle_list_scenes(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/scenes)",
        [this](const auto& req, auto& res) { handle_scene_new(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/scenes/([^/]+))",
        [this](const auto& req, auto& res) { handle_get_scene(req, res); });
    server.Patch(R"(/api/worldbuilding/([^/]+)/scenes/([^/]+))",
        [this](const auto& req, auto& res) { handle_patch_scene(req, res); });
    server.Delete(R"(/api/worldbuilding/([^/]+)/scenes/([^/]+))",
        [this](const auto& req, auto& res) { handle_delete_scene(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/scenes/([^/]+)/end)",
        [this](const auto& req, auto& res) { handle_scene_end(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/scenes/([^/]+)/extraction-result)",
        [this](const auto& req, auto& res) { handle_scene_extraction_result(req, res); });

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
    server.Delete(R"(/api/worldbuilding/([^/]+)/foreshadowing/([^/]+))",
        [this](const auto& req, auto& res) { handle_delete_foreshadowing(req, res); });

    // Secret
    server.Get(R"(/api/worldbuilding/([^/]+)/secrets)",
        [this](const auto& req, auto& res) { handle_secret_list(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/secrets)",
        [this](const auto& req, auto& res) { handle_secret_create(req, res); });
    server.Patch(R"(/api/worldbuilding/([^/]+)/secrets/([^/]+))",
        [this](const auto& req, auto& res) { handle_patch_secret(req, res); });
    server.Delete(R"(/api/worldbuilding/([^/]+)/secrets/([^/]+))",
        [this](const auto& req, auto& res) { handle_delete_secret(req, res); });

    // Locations
    server.Get(R"(/api/worldbuilding/([^/]+)/locations)",
        [this](const auto& req, auto& res) { handle_list_locations(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/locations/([^/]+))",
        [this](const auto& req, auto& res) { handle_get_location(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/locations)",
        [this](const auto& req, auto& res) { handle_create_location(req, res); });
    server.Patch(R"(/api/worldbuilding/([^/]+)/locations/([^/]+))",
        [this](const auto& req, auto& res) {
            std::string world_id = req.matches[1];
            std::string loc_id = req.matches[2];
            try {
                auto body = nlohmann::json::parse(req.body);
                auto fields = body.at("fields");
                bool ok = service_->worlds().update_location(world_id, loc_id, fields);
                if (!ok) { error_response(res, "Location not found", 404, "location_not_found"); return; }
                json_response(res, {{"ok", true}});
            } catch (const std::exception& e) { error_response(res, e.what(), 400); }
        });
    server.Delete(R"(/api/worldbuilding/([^/]+)/locations/([^/]+))",
        [this](const auto& req, auto& res) { handle_delete_location(req, res); });

    // Knowledge
    server.Get(R"(/api/worldbuilding/([^/]+)/knowledge)",
        [this](const auto& req, auto& res) { handle_list_knowledge(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/knowledge)",
        [this](const auto& req, auto& res) { handle_create_knowledge(req, res); });
    server.Patch(R"(/api/worldbuilding/([^/]+)/knowledge/([^/]+))",
        [this](const auto& req, auto& res) { handle_update_knowledge(req, res); });
    server.Delete(R"(/api/worldbuilding/([^/]+)/knowledge/([^/]+))",
        [this](const auto& req, auto& res) { handle_delete_knowledge(req, res); });

    // Factions
    server.Get(R"(/api/worldbuilding/([^/]+)/factions)",
        [this](const auto& req, auto& res) { handle_list_factions(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/factions/([^/]+))",
        [this](const auto& req, auto& res) { handle_get_faction(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/factions)",
        [this](const auto& req, auto& res) { handle_create_faction(req, res); });
    server.Patch(R"(/api/worldbuilding/([^/]+)/factions/([^/]+))",
        [this](const auto& req, auto& res) { handle_update_faction(req, res); });
    server.Delete(R"(/api/worldbuilding/([^/]+)/factions/([^/]+))",
        [this](const auto& req, auto& res) { handle_delete_faction(req, res); });

    // Dashboard
    server.Post(R"(/api/worldbuilding/([^/]+)/dashboard)",
        [this](const auto& req, auto& res) { handle_dashboard(req, res); });

    // File links
    server.Get(R"(/api/worldbuilding/([^/]+)/files)",
        [this](const auto& req, auto& res) { handle_list_file_links(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/files)",
        [this](const auto& req, auto& res) { handle_create_file_link(req, res); });
    server.Delete(R"(/api/worldbuilding/([^/]+)/files/([^/]+))",
        [this](const auto& req, auto& res) { handle_delete_file_link(req, res); });

    // ─── Preview builders (Agent-driven) ───
    server.Post(R"(/api/worldbuilding/([^/]+)/previews/([^/]+))",
        [this](const auto& req, auto& res) { handle_build_preview(req, res); });

    // ─── Pending creations (Agent-driven) ───
    server.Post(R"(/api/worldbuilding/([^/]+)/creations)",
        [this](const auto& req, auto& res) { handle_store_pending_creation(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/creations/([^/]+))",
        [this](const auto& req, auto& res) { handle_get_pending_creation(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/creations/([^/]+)/resolve)",
        [this](const auto& req, auto& res) { handle_resolve_creation(req, res); });

    // ─── Agent-driven generation ───
    server.Post(R"(/api/worldbuilding/([^/]+)/suggestions)",
        [this](const auto& req, auto& res) { handle_start_suggestions(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/suggestions)",
        [this](const auto& req, auto& res) { handle_get_suggestions(req, res); });

    server.Post(R"(/api/worldbuilding/([^/]+)/check-consistency)",
        [this](const auto& req, auto& res) { handle_start_consistency_check(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/check-consistency)",
        [this](const auto& req, auto& res) { handle_get_consistency_check(req, res); });

    // ─── Agent-driven: generate scenes ───
    server.Post(R"(/api/worldbuilding/([^/]+)/chapters/([^/]+)/generate-scenes)",
        [this](const auto& req, auto& res) { handle_start_generate_scenes(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/chapters/([^/]+)/generated-scenes)",
        [this](const auto& req, auto& res) { handle_get_generated_scenes(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/chapters/([^/]+)/generated-scenes/apply)",
        [this](const auto& req, auto& res) { handle_apply_generated_scenes(req, res); });

    // ─── Agent-driven: generate outline ───
    server.Post(R"(/api/worldbuilding/([^/]+)/generate-outline)",
        [this](const auto& req, auto& res) { handle_start_generate_outline(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/generated-outline)",
        [this](const auto& req, auto& res) { handle_get_generated_outline(req, res); });
    server.Post(R"(/api/worldbuilding/([^/]+)/generated-outline/apply)",
        [this](const auto& req, auto& res) { handle_apply_generated_outline(req, res); });

    // ─── Agent-driven: scene rewrite ───
    server.Post(R"(/api/worldbuilding/([^/]+)/scenes/([^/]+)/rewrite)",
        [this](const auto& req, auto& res) { handle_start_rewrite_scene(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/scenes/([^/]+)/rewrite-result)",
        [this](const auto& req, auto& res) { handle_get_rewrite_result(req, res); });

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

    server.Post(R"(/api/worldbuilding/([^/]+)/pipeline/retreat)",
        [this](const auto& req, auto& res) { handle_pipeline_retreat(req, res); });

    server.Post(R"(/api/worldbuilding/([^/]+)/pipeline/clear-error)",
        [this](const auto& req, auto& res) { handle_pipeline_clear_error(req, res); });

    // Knowledge Graph
    server.Get(R"(/api/worldbuilding/([^/]+)/knowledge-graph/entities)",
        [this](const auto& req, auto& res) { handle_kg_entities(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/knowledge-graph/entities/([^/]+)/relations)",
        [this](const auto& req, auto& res) { handle_kg_entity_relations(req, res); });
    server.Get(R"(/api/worldbuilding/([^/]+)/knowledge-graph/search)",
        [this](const auto& req, auto& res) { handle_kg_search(req, res); });
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
        if (!service_->worlds().delete_world(world_id)) {
            error_response(res, "World not found", 404, "world_not_found");
            return;
        }
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
        // Optional kind filter (individual, group, god, etc.)
        std::optional<std::string> kind_filter;
        if (req.has_param("kind") && !req.get_param_value("kind").empty()) {
            kind_filter = req.get_param_value("kind");
        }
        auto agents = service_->agents().list_agents(wid, kind_filter);
        // Optional faction filter: intersect with faction members
        if (req.has_param("faction") && !req.get_param_value("faction").empty()) {
            std::string faction_id = req.get_param_value("faction");
            auto faction = service_->worlds().get_faction(wid, faction_id);
            if (faction) {
                std::set<std::string> member_set(faction->member_agent_ids.begin(),
                                                  faction->member_agent_ids.end());
                auto new_end = std::remove_if(agents.begin(), agents.end(),
                    [&](const auto& a) { return member_set.find(a.id) == member_set.end(); });
                agents.erase(new_end, agents.end());
            }
        }
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

void WorldbuildingHttpHandler::handle_chapter_tree(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];

        if (!service_->worlds().get_world(wid)) {
            error_response(res, "World not found", 404, "world_not_found");
            return;
        }

        auto arcs = service_->narrative().list_arcs(wid);
        auto chapters = service_->narrative().list_chapters(wid);

        // Group chapters by arc_id
        std::unordered_map<std::string, nlohmann::json> chapters_by_arc;
        nlohmann::json orphan_chapters = nlohmann::json::array();

        for (const auto& ch : chapters) {
            nlohmann::json ch_json = chapter_json(ch);
            if (ch.arc_id.has_value() && !ch.arc_id->empty()) {
                chapters_by_arc[*ch.arc_id].push_back(ch_json);
            } else {
                orphan_chapters.push_back(ch_json);
            }
        }

        auto sort_by_number = [](const nlohmann::json& a, const nlohmann::json& b) {
            return a.value("number", 0) < b.value("number", 0);
        };

        nlohmann::json tree = nlohmann::json::array();
        for (const auto& arc : arcs) {
            auto it = chapters_by_arc.find(arc.id);
            auto chapter_arr = it != chapters_by_arc.end() ? it->second : nlohmann::json::array();
            std::sort(chapter_arr.begin(), chapter_arr.end(), sort_by_number);

            nlohmann::json arc_node{
                {"id", arc.id},
                {"title", arc.title},
                {"status", arc.status},
                {"chapters", chapter_arr}
            };
            tree.push_back(arc_node);
        }

        if (!orphan_chapters.empty()) {
            std::sort(orphan_chapters.begin(), orphan_chapters.end(), sort_by_number);
            nlohmann::json uncategorized{
                {"id", ""},
                {"title", "Uncategorized"},
                {"status", ""},
                {"chapters", orphan_chapters}
            };
            tree.push_back(uncategorized);
        }

        json_response(res, {{"ok", true}, {"tree", tree}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
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

void WorldbuildingHttpHandler::handle_agent_diary_get(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string aid = req.matches[2];
        std::string did = req.matches[3];
        auto agent = service_->agents().get_agent(aid);
        if (!agent) {
            error_response(res, "Agent not found", 404, "agent_not_found");
            return;
        }
        if (agent->world_id != wid) {
            error_response(res, "Agent not found in this world", 404, "agent_not_found");
            return;
        }
        auto diary = service_->agents().get_diary(did);
        if (!diary) {
            error_response(res, "Diary not found", 404, "diary_not_found");
            return;
        }
        json_response(res, {{"ok", true}, {"diary", {
            {"id", diary->id},
            {"agent_id", diary->agent_id},
            {"scene_id", diary->scene_id},
            {"world_time", diary->world_time},
            {"content", diary->content},
            {"mood", diary->mood},
            {"status", diary->status},
            {"leak_risk_level", diary->leak_risk_level},
            {"tokens_used", diary->tokens_used},
            {"created_at", diary->created_at}
        }}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_agent_diary_patch(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    std::string aid = req.matches[2];
    std::string did = req.matches[3];
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
        auto diary = service_->agents().get_diary(did);
        if (!diary) {
            error_response(res, "Diary not found", 404, "diary_not_found");
            return;
        }
        auto body = nlohmann::json::parse(req.body);
        std::string content = body.value("content", diary->content);
        std::string mood = body.value("mood", diary->mood);
        int leak_risk_level = body.value("leak_risk_level", diary->leak_risk_level);
        int tokens_used = body.value("tokens_used", diary->tokens_used);
        service_->agents().update_diary_content(did, content, mood, leak_risk_level, tokens_used);
        json_response(res, {{"ok", true}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_agent_diary_search(const httplib::Request& req, httplib::Response& res) {
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
        if (!req.has_param("q")) {
            error_response(res, "Missing required query parameter: q", 400, "missing_param");
            return;
        }
        std::string q = req.get_param_value("q");
        int limit = 20;
        if (req.has_param("limit")) {
            try {
                limit = std::stoi(req.get_param_value("limit"));
            } catch (...) {
                error_response(res, "Invalid limit parameter", 400);
                return;
            }
        }
        auto diaries = service_->agents().search_diary(aid, q, limit);
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
        json_response(res, {{"ok", true}, {"diaries", arr}, {"total", diaries.size()}});
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

// --- Agent relation create ---

void WorldbuildingHttpHandler::handle_agent_relation_upsert(const httplib::Request& req, httplib::Response& res) {
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
        auto body = nlohmann::json::parse(req.body);
        if (!body.contains("target_id") || !body["target_id"].is_string()) {
            error_response(res, "target_id is required", 400);
            return;
        }
        worldbuilding::RelationEntry rel;
        rel.agent_id = aid;
        rel.target_id = body["target_id"].get<std::string>();
        rel.relation_type = body.value("relation_type", "");
        rel.description = body.value("description", "");
        rel.intimacy = body.value("intimacy", 0);
        service_->agents().upsert_relation(rel);
        json_response(res, {{"ok", true}}, 201);
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Agent relation update ---

void WorldbuildingHttpHandler::handle_agent_relation_update(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string aid = req.matches[2];
        std::string tid = req.matches[3];
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
        worldbuilding::RelationEntry rel;
        rel.agent_id = aid;
        rel.target_id = tid;
        if (body.contains("relation_type")) rel.relation_type = body["relation_type"].get<std::string>();
        if (body.contains("description")) rel.description = body["description"].get<std::string>();
        if (body.contains("intimacy")) rel.intimacy = body["intimacy"].get<int>();
        service_->agents().upsert_relation(rel);
        json_response(res, {{"ok", true}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Agent voice fingerprint ---

void WorldbuildingHttpHandler::handle_agent_voice(const httplib::Request& req, httplib::Response& res) {
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
        auto fp = service_->voice().fingerprint_for(aid);
        if (!fp) {
            error_response(res, "Voice fingerprint not found", 404, "not_found");
            return;
        }
        json_response(res, {
            {"ok", true},
            {"voice", {
                {"avg_sentence_length", fp->avg_sentence_length},
                {"sentence_variance", fp->sentence_variance},
                {"question_frequency", fp->question_frequency},
                {"modifier_ratio", fp->modifier_ratio},
                {"sample_count", fp->sample_count},
                {"signature_words", fp->signature_words},
                {"tone_profile", fp->tone_profile}
            }}
        });
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Memory summaries ---

void WorldbuildingHttpHandler::handle_memory_summaries_list(const httplib::Request& req, httplib::Response& res) {
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
        int limit = 50;
        if (req.has_param("limit")) {
            try {
                limit = std::stoi(req.get_param_value("limit"));
            } catch (...) {
                error_response(res, "Invalid limit parameter", 400);
                return;
            }
        }
        auto summaries = service_->agents().recent_summaries(aid, limit);
        nlohmann::json arr = nlohmann::json::array();
        for (auto& s : summaries) {
            arr.push_back({
                {"id", s.id},
                {"period_start", s.period_start},
                {"period_end", s.period_end},
                {"summary", s.summary},
                {"source_diary_ids", s.source_diary_ids},
                {"created_at", s.created_at}
            });
        }
        json_response(res, {{"ok", true}, {"summaries", arr}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_memory_summary_get(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string aid = req.matches[2];
        std::string mid = req.matches[3];
        auto agent = service_->agents().get_agent(aid);
        if (!agent) {
            error_response(res, "Agent not found", 404, "agent_not_found");
            return;
        }
        if (agent->world_id != wid) {
            error_response(res, "Agent not found in this world", 404, "agent_not_found");
            return;
        }
        auto summaries = service_->agents().recent_summaries(aid, 200);
        const worldbuilding::MemorySummary* found = nullptr;
        for (auto& s : summaries) {
            if (s.id == mid) {
                found = &s;
                break;
            }
        }
        if (!found) {
            error_response(res, "Memory summary not found", 404, "memory_summary_not_found");
            return;
        }
        json_response(res, {{"ok", true}, {"summary", {
            {"id", found->id},
            {"period_start", found->period_start},
            {"period_end", found->period_end},
            {"summary", found->summary},
            {"source_diary_ids", found->source_diary_ids},
            {"created_at", found->created_at}
        }}});
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

// --- Export ---

void WorldbuildingHttpHandler::handle_export_chapters(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string world_id = req.matches[1];
        auto body = nlohmann::json::parse(req.body);

        if (!body.contains("chapter_ids") || !body["chapter_ids"].is_array() || body["chapter_ids"].empty()) {
            error_response(res, "Missing or empty chapter_ids array", 400, "missing_chapter_ids");
            return;
        }

        std::vector<std::string> chapter_ids;
        for (const auto& cid : body["chapter_ids"]) {
            if (cid.is_string()) {
                chapter_ids.push_back(cid.get<std::string>());
            }
        }

        if (chapter_ids.empty()) {
            error_response(res, "No valid chapter IDs provided", 400, "missing_chapter_ids");
            return;
        }

        std::string title = body.value("title", "");
        std::string author = body.value("author", "");

        auto result = service_->export_chapters(world_id, chapter_ids, title, author);

        json_response(res, {
            {"ok", true},
            {"file_path", result.file_path},
            {"total_chars", result.total_chars}
        });
    } catch (const std::exception& e) {
        error_response(res, e.what(), 500, "export_failed");
    }
}

void WorldbuildingHttpHandler::handle_export_full(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string world_id = req.matches[1];

        bool include_diaries = false;
        bool include_memories = false;
        if (!req.body.empty()) {
            auto body = nlohmann::json::parse(req.body);
            include_diaries = body.value("include_diaries", false);
            include_memories = body.value("include_memories", false);
        }

        auto snapshot = service_->export_world_snapshot(world_id, include_diaries, include_memories);

        // Enrich with image data if image_service_ is available
        if (image_service_ && snapshot.payload.contains("agents") && snapshot.payload["agents"].is_array()) {
            for (auto& agent_json : snapshot.payload["agents"]) {
                if (!agent_json.contains("id")) continue;
                std::string agent_id = agent_json["id"].get<std::string>();

                auto images = image_service_->list_images(agent_id);
                nlohmann::json imgs_arr = nlohmann::json::array();
                for (const auto& img : images) {
                    nlohmann::json img_json;
                    img_json["id"] = img.id;
                    img_json["agent_id"] = img.agent_id;
                    img_json["image_type"] = img.image_type;
                    img_json["storage_key"] = img.storage_key;
                    img_json["mime_type"] = img.mime_type;
                    img_json["original_name"] = img.original_name;
                    img_json["file_size_bytes"] = img.file_size_bytes;
                    img_json["is_primary"] = img.is_primary;
                    img_json["sort_order"] = img.sort_order;
                    img_json["created_at"] = img.created_at;

                    // Inline small images as base64, mark large ones as external
                    const int64_t kMaxInlineBytes = 256 * 1024; // 256 KB
                    if (img.file_size_bytes > 0 && img.file_size_bytes <= kMaxInlineBytes) {
                        try {
                            auto image_data = image_service_->store().load(img.storage_key);
                            if (!image_data.bytes.empty()) {
                                std::string b64 = worldbuilding::base64_encode(image_data.bytes);
                                img_json["data"] = "data:" + img.mime_type + ";base64," + b64;
                            } else {
                                img_json["data"] = nullptr;
                            }
                        } catch (...) {
                            img_json["data"] = nullptr;
                            img_json["transfer"] = "external";
                        }
                    } else {
                        img_json["data"] = nullptr;
                        img_json["transfer"] = "external";
                    }
                    imgs_arr.push_back(img_json);
                }
                agent_json["images"] = imgs_arr;

                // Update image count in manifest
                if (!images.empty()) {
                    int existing = snapshot.manifest.value("image_count", 0);
                    snapshot.manifest["image_count"] = existing + static_cast<int>(images.size());
                }
            }
            // Ensure image count is present even if zero
            if (!snapshot.manifest.contains("image_count")) {
                snapshot.manifest["image_count"] = 0;
            }
        }

        json_response(res, {
            {"ok", true},
            {"snapshot", {
                {"schema_version", snapshot.schema_version},
                {"exported_at", snapshot.exported_at},
                {"snapshot_id", snapshot.snapshot_id},
                {"source", snapshot.source},
                {"manifest", snapshot.manifest},
                {"payload", snapshot.payload}
            }}
        });
    } catch (const std::exception& e) {
        error_response(res, e.what(), 500, "export_failed");
    }
}

void WorldbuildingHttpHandler::handle_import_snapshot(const httplib::Request& req, httplib::Response& res) {
    try {
        auto body = nlohmann::json::parse(req.body);
        if (!body.contains("snapshot")) {
            error_response(res, "missing required field: snapshot", 400, "missing_field");
            return;
        }
        const auto& snapshot = body["snapshot"];
        std::optional<std::string> target_name;
        if (body.contains("target_name") && body["target_name"].is_string()) {
            target_name = body["target_name"].get<std::string>();
        }

        auto result = service_->import_snapshot(snapshot, target_name);

        // Import images if image_service_ is available and snapshot has agents
        int images_imported = 0;
        if (image_service_ && snapshot.contains("payload")) {
            const auto& payload = snapshot["payload"];
            if (payload.contains("agents") && payload["agents"].is_array()) {
                for (const auto& agent_json : payload["agents"]) {
                    if (!agent_json.contains("id") || !agent_json.contains("images")) continue;
                    if (!agent_json["images"].is_array()) continue;

                    std::string old_agent_id = agent_json["id"].get<std::string>();
                    auto it = result.id_mapping.find(old_agent_id);
                    if (it == result.id_mapping.end()) continue;
                    std::string new_agent_id = it->second;

                    for (const auto& img_json : agent_json["images"]) {
                        if (!img_json.contains("data") || img_json["data"].is_null()) continue;
                        std::string data_uri = img_json["data"].get<std::string>();
                        // Only import inline images (skip external references)
                        if (data_uri.empty() || data_uri.find("data:") != 0) continue;

                        // Parse data URI: data:<mime_type>;base64,<data>
                        auto comma_pos = data_uri.find(',');
                        if (comma_pos == std::string::npos) continue;
                        std::string header = data_uri.substr(0, comma_pos);
                        std::string b64_data = data_uri.substr(comma_pos + 1);

                        std::string mime_type = img_json.value("mime_type", "image/png");
                        // Extract mime from header: "data:image/png;base64"
                        auto colon_pos = header.find(':');
                        auto semi_pos = header.find(';');
                        if (colon_pos != std::string::npos && semi_pos != std::string::npos) {
                            mime_type = header.substr(colon_pos + 1, semi_pos - colon_pos - 1);
                        }

                        auto decoded = worldbuilding::base64_decode(b64_data);
                        if (!decoded || decoded->empty()) continue;

                        try {
                            image_service_->upload(
                                result.world_id,
                                new_agent_id,
                                img_json.value("image_type", "avatar"),
                                img_json.value("original_name", "imported"),
                                mime_type,
                                *decoded);
                            images_imported++;
                        } catch (const std::exception& e) {
                            spdlog::warn("import_snapshot: failed to import image for agent {}: {}",
                                         new_agent_id, e.what());
                        }
                    }
                }
            }
        }

        nlohmann::json response = {
            {"ok", true},
            {"world_id", result.world_id},
            {"id_mapping", result.id_mapping},
            {"images_imported", images_imported}
        };
        json_response(res, response, 201);

    } catch (const std::exception& e) {
        error_response(res, e.what(), 500, "import_failed");
    }
}

// --- Delete agent ---

void WorldbuildingHttpHandler::handle_delete_agent(const httplib::Request& req, httplib::Response& res) {
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
        service_->agents().delete_agent(aid);
        json_response(res, {{"ok", true}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Delete chapter / scene ---

void WorldbuildingHttpHandler::handle_delete_chapter(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    std::string cid = req.matches[2];
    try {
        if (!service_->narrative().delete_chapter(wid, cid)) {
            error_response(res, "Chapter not found", 404, "chapter_not_found");
            return;
        }
        json_response(res, {{"ok", true}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_delete_scene(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    std::string sid = req.matches[2];
    try {
        if (!service_->narrative().delete_scene(wid, sid)) {
            error_response(res, "Scene not found", 404, "scene_not_found");
            return;
        }
        json_response(res, {{"ok", true}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_get_scene(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string sid = req.matches[2];
        auto scene = service_->narrative().get_scene(wid, sid);
        if (!scene) {
            error_response(res, "Scene not found", 404, "scene_not_found");
            return;
        }
        nlohmann::json j{
            {"id", scene->id},
            {"title", scene->title},
            {"chapter_id", scene->chapter_id},
            {"world_time", scene->world_time},
            {"narrative", scene->narrative},
            {"status", worldbuilding::to_string(scene->status)},
            {"participant_ids", scene->participant_ids},
            {"plot_goal", scene->plot_goal},
            {"emotional_goal", scene->emotional_goal},
            {"information_goal", scene->information_goal},
            {"external_conflict", scene->external_conflict},
            {"internal_conflict", scene->internal_conflict},
            {"foreshadowing_ids", scene->foreshadowing_ids},
            {"style_overrides", scene->style_overrides}
        };
        if (scene->section_id) j["section_id"] = *scene->section_id;
        else j["section_id"] = nullptr;
        if (scene->location_id) j["location_id"] = *scene->location_id;
        else j["location_id"] = nullptr;
        if (scene->pov_character_id) j["pov_character_id"] = *scene->pov_character_id;
        else j["pov_character_id"] = nullptr;
        if (scene->hidden_conflict) j["hidden_conflict"] = *scene->hidden_conflict;
        else j["hidden_conflict"] = nullptr;
        json_response(res, {{"ok", true}, {"scene", j}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_reorder_chapters(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    try {
        auto body = nlohmann::json::parse(req.body);
        auto order = body.at("order");
        bool ok = service_->narrative().reorder_chapters(wid, order);
        if (!ok) {
            error_response(res, "Reorder failed", 400);
            return;
        }
        json_response(res, {{"ok", true}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Delete foreshadowing / secret ---

void WorldbuildingHttpHandler::handle_delete_foreshadowing(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    std::string fid = req.matches[2];
    try {
        bool ok = service_->foreshadowing().delete_foreshadowing(wid, fid);
        if (!ok) {
            error_response(res, "Foreshadowing not found", 404, "foreshadow_not_found");
            return;
        }
        json_response(res, {{"ok", true}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_delete_secret(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    std::string sid = req.matches[2];
    try {
        bool ok = service_->secrets().delete_secret(wid, sid);
        if (!ok) {
            error_response(res, "Secret not found", 404, "secret_not_found");
            return;
        }
        json_response(res, {{"ok", true}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Location handlers ---

void WorldbuildingHttpHandler::handle_list_locations(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto locations = service_->worlds().list_locations(wid);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& loc : locations) {
            arr.push_back({
                {"id", loc.id}, {"name", loc.name}, {"description", loc.description},
                {"region", loc.region}, {"parent_location_id", loc.parent_location_id.has_value()
                    ? nlohmann::json(*loc.parent_location_id) : nlohmann::json(nullptr)},
                {"created_at", loc.created_at}
            });
        }
        json_response(res, {{"ok", true}, {"locations", arr}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_get_location(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string lid = req.matches[2];
        auto loc = service_->worlds().get_location(wid, lid);
        if (!loc) {
            error_response(res, "Location not found", 404, "location_not_found");
            return;
        }
        json_response(res, {{"ok", true}, {"location", {
            {"id", loc->id}, {"name", loc->name}, {"description", loc->description},
            {"region", loc->region}, {"parent_location_id", loc->parent_location_id.has_value()
                ? nlohmann::json(*loc->parent_location_id) : nlohmann::json(nullptr)},
            {"created_at", loc->created_at}
        }}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_create_location(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto body = nlohmann::json::parse(req.body);
        worldbuilding::Location loc;
        loc.name = body.at("name").get<std::string>();
        loc.description = body.value("description", "");
        loc.region = body.value("region", "");
        if (body.contains("parent_location_id") && !body["parent_location_id"].is_null())
            loc.parent_location_id = body["parent_location_id"].get<std::string>();
        auto created = service_->worlds().add_location(wid, loc);
        json_response(res, {{"ok", true}, {"location_id", created.id}}, 201);
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_delete_location(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    std::string lid = req.matches[2];
    try {
        bool ok = service_->worlds().delete_location(wid, lid);
        if (!ok) {
            error_response(res, "Location not found", 404, "location_not_found");
            return;
        }
        json_response(res, {{"ok", true}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Knowledge handlers ---

void WorldbuildingHttpHandler::handle_list_knowledge(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string category = req.has_param("category") ? req.get_param_value("category") : "";
        auto items = service_->worlds().get_world_knowledge(wid, category);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& k : items) {
            arr.push_back({
                {"id", k.id}, {"category", k.category}, {"content", k.content},
                {"tags", k.tags}, {"aliases", k.aliases}, {"related_ids", k.related_ids},
                {"created_at", k.created_at}
            });
        }
        json_response(res, {{"ok", true}, {"knowledge", arr}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_create_knowledge(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto body = nlohmann::json::parse(req.body);
        worldbuilding::WorldKnowledge item;
        item.category = body.at("category").get<std::string>();
        item.content = body.at("content").get<std::string>();
        if (body.contains("tags") && body["tags"].is_array())
            for (const auto& t : body["tags"]) item.tags.push_back(t.get<std::string>());
        if (body.contains("aliases") && body["aliases"].is_array())
            for (const auto& a : body["aliases"]) item.aliases.push_back(a.get<std::string>());
        if (body.contains("related_ids") && body["related_ids"].is_array())
            for (const auto& r : body["related_ids"]) item.related_ids.push_back(r.get<std::string>());
        service_->worlds().add_world_knowledge(wid, item);
        json_response(res, {{"ok", true}}, 201);
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_update_knowledge(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    std::string kid = req.matches[2];
    try {
        auto body = nlohmann::json::parse(req.body);
        auto fields = body.at("fields");
        bool ok = service_->worlds().update_knowledge(wid, kid, fields);
        if (!ok) {
            error_response(res, "Knowledge not found", 404, "knowledge_not_found");
            return;
        }
        json_response(res, {{"ok", true}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_delete_knowledge(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    std::string kid = req.matches[2];
    try {
        bool ok = service_->worlds().delete_knowledge(wid, kid);
        if (!ok) {
            error_response(res, "Knowledge not found", 404, "knowledge_not_found");
            return;
        }
        json_response(res, {{"ok", true}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Faction handlers ---

void WorldbuildingHttpHandler::handle_list_factions(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto factions = service_->worlds().list_factions(wid);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& f : factions) {
            arr.push_back({
                {"id", f.id}, {"world_id", f.world_id}, {"name", f.name},
                {"description", f.description}, {"goals", f.goals},
                {"member_agent_ids", f.member_agent_ids},
                {"rival_faction_ids", f.rival_faction_ids},
                {"created_at", f.created_at}, {"updated_at", f.updated_at}
            });
        }
        json_response(res, {{"ok", true}, {"factions", arr}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_get_faction(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string fid = req.matches[2];
        auto faction = service_->worlds().get_faction(wid, fid);
        if (!faction) {
            error_response(res, "Faction not found", 404, "faction_not_found");
            return;
        }
        json_response(res, {{"ok", true}, {"faction", {
            {"id", faction->id}, {"world_id", faction->world_id}, {"name", faction->name},
            {"description", faction->description}, {"goals", faction->goals},
            {"member_agent_ids", faction->member_agent_ids},
            {"rival_faction_ids", faction->rival_faction_ids},
            {"created_at", faction->created_at}, {"updated_at", faction->updated_at}
        }}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_create_faction(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto body = nlohmann::json::parse(req.body);
        worldbuilding::Faction faction;
        faction.name = body.at("name").get<std::string>();
        faction.description = body.value("description", "");
        faction.goals = body.value("goals", "");
        if (body.contains("member_agent_ids") && body["member_agent_ids"].is_array())
            for (const auto& m : body["member_agent_ids"])
                faction.member_agent_ids.push_back(m.get<std::string>());
        if (body.contains("rival_faction_ids") && body["rival_faction_ids"].is_array())
            for (const auto& r : body["rival_faction_ids"])
                faction.rival_faction_ids.push_back(r.get<std::string>());
        auto created = service_->worlds().add_faction(wid, faction);
        json_response(res, {{"ok", true}, {"faction_id", created.id}}, 201);
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_update_faction(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    std::string fid = req.matches[2];
    try {
        auto body = nlohmann::json::parse(req.body);
        auto fields = body.at("fields");
        bool ok = service_->worlds().update_faction(wid, fid, fields);
        if (!ok) {
            error_response(res, "Faction not found", 404, "faction_not_found");
            return;
        }
        json_response(res, {{"ok", true}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_delete_faction(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    std::string fid = req.matches[2];
    try {
        bool ok = service_->worlds().delete_faction(wid, fid);
        if (!ok) {
            error_response(res, "Faction not found", 404, "faction_not_found");
            return;
        }
        json_response(res, {{"ok", true}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Dashboard ---

void WorldbuildingHttpHandler::handle_dashboard(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto dash = service_->get_dashboard(wid);
        json_response(res, {{"ok", true}, {"dashboard", dash}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- File links ---

void WorldbuildingHttpHandler::handle_list_file_links(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto links = service_->worlds().list_file_links(wid);
        json_response(res, {{"ok", true}, {"files", links}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_create_file_link(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto body = nlohmann::json::parse(req.body);
        service_->worlds().add_file_link(wid,
            body.at("file_path").get<std::string>(),
            body.at("target_type").get<std::string>(),
            body.at("target_id").get<std::string>());
        json_response(res, {{"ok", true}}, 201);
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_delete_file_link(const httplib::Request& req, httplib::Response& res) {
    std::string wid = req.matches[1];
    std::string file_path = req.matches[2];
    try {
        auto body = nlohmann::json::parse(req.body);
        bool ok = service_->worlds().remove_file_link(wid, file_path,
            body.at("target_type").get<std::string>(),
            body.at("target_id").get<std::string>());
        if (!ok) {
            error_response(res, "File link not found", 404, "file_link_not_found");
            return;
        }
        json_response(res, {{"ok", true}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Preview builders (Agent-driven) ---

void WorldbuildingHttpHandler::handle_build_preview(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string tool_name = req.matches[2];
        auto body = nlohmann::json::parse(req.body);
        auto params = body.value("params", body);

        nlohmann::json preview;
        if (tool_name == "create_scene")
            preview = service_->build_scene_preview(wid, params);
        else if (tool_name == "create_chapter")
            preview = service_->build_chapter_preview(wid, params);
        else if (tool_name == "create_arc")
            preview = service_->build_arc_preview(wid, params);
        else if (tool_name == "create_secret")
            preview = service_->build_secret_preview(wid, params);
        else if (tool_name == "add_world_knowledge")
            preview = service_->build_world_knowledge_preview(wid, params);
        else if (tool_name == "add_location")
            preview = service_->build_location_preview(wid, params);
        else {
            error_response(res, "Unknown preview tool: " + tool_name, 400, "unknown_tool");
            return;
        }

        json_response(res, {{"ok", true}, {"preview", preview}, {"tool_name", tool_name}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Pending creations (Agent-driven) ---

void WorldbuildingHttpHandler::handle_store_pending_creation(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto body = nlohmann::json::parse(req.body);
        auto creation_id = service_->store_pending_creation(
            wid,
            body.at("tool_name").get<std::string>(),
            body.at("params"),
            body.at("preview"));
        json_response(res, {{"ok", true}, {"creation_id", creation_id}}, 201);
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_get_pending_creation(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string creation_id = req.matches[2];
        auto pending = service_->get_pending_creation(creation_id);
        if (!pending) {
            error_response(res, "Pending creation not found", 404, "creation_not_found");
            return;
        }
        json_response(res, {
            {"ok", true},
            {"creation", {
                {"creation_id", pending->creation_id},
                {"tool_name", pending->tool_name},
                {"world_id", pending->world_id},
                {"params", pending->params},
                {"preview", pending->preview}
            }}
        });
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_resolve_creation(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string creation_id = req.matches[2];
        auto body = nlohmann::json::parse(req.body);
        auto decision = body.at("decision").get<std::string>();
        auto modifications = body.value("modifications", nlohmann::json::object());
        auto result = service_->resolve_creation(creation_id, decision, modifications);
        json_response(res, {{"ok", true}, {"result", result}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Agent-driven: suggestions ---

void WorldbuildingHttpHandler::handle_start_suggestions(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto body = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
        std::string category = body.value("category", "all");
        std::string task = "分析世界设定，找出 " + category + " 类别的缺口，产出结构化建议列表。";
        start_agent_run(req, res, wid, task, "suggestions");
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_get_suggestions(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto result = service_->worlds().get_agent_result(wid, "suggestions");
        if (!result) {
            error_response(res, "Suggestions not yet generated", 404, "result_not_found");
            return;
        }
        json_response(res, {{"ok", true}, {"suggestions", *result}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Agent-driven: consistency check ---

void WorldbuildingHttpHandler::handle_start_consistency_check(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto body = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
        std::string scope = body.value("scope", "all");
        std::string task = "检查世界一致性，范围：" + scope + "。按 narrative_rules.md 规则检查矛盾（如角色死亡后出场、时间线冲突、阵营关系矛盾），产出结构化冲突列表。";
        start_agent_run(req, res, wid, task, "consistency_check");
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_get_consistency_check(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto result = service_->worlds().get_agent_result(wid, "consistency_check");
        if (!result) {
            error_response(res, "Consistency check not yet generated", 404, "result_not_found");
            return;
        }
        json_response(res, {{"ok", true}, {"conflicts", *result}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Agent-driven: generate scenes ---

void WorldbuildingHttpHandler::handle_start_generate_scenes(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string cid = req.matches[2];
        auto body = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
        int scene_count = body.value("scene_count", 0);
        std::string task = "为本章节拆分场景（";
        if (scene_count > 0) task += std::to_string(scene_count) + "个";
        task += "），产出每个场景的 plot_goal、emotional_goal、information_goal 和 suggested_participants。";
        start_agent_run(req, res, wid, task, "generated_scenes:" + cid);
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_get_generated_scenes(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string cid = req.matches[2];
        auto result = service_->worlds().get_agent_result(wid, "generated_scenes:" + cid);
        if (!result) {
            error_response(res, "Scenes not yet generated", 404, "result_not_found");
            return;
        }
        json_response(res, {{"ok", true}, {"scenes", *result}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_apply_generated_scenes(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string cid = req.matches[2];
        auto body = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
        auto result = service_->worlds().get_agent_result(wid, "generated_scenes:" + cid);
        if (!result) {
            error_response(res, "No generated scenes to apply", 404, "result_not_found");
            return;
        }
        nlohmann::json scenes = result->is_array() ? *result : (*result)["scenes"];
        std::set<int> selected;
        if (body.contains("selected_indices") && body["selected_indices"].is_array()) {
            for (const auto& idx : body["selected_indices"]) selected.insert(idx.get<int>());
        }
        nlohmann::json scene_ids = nlohmann::json::array();
        int idx = 0;
        for (const auto& scene_data : scenes) {
            if (!selected.empty() && !selected.contains(idx)) { idx++; continue; }
            worldbuilding::Scene scene;
            scene.title = scene_data.value("title", "未命名场景");
            scene.chapter_id = cid;
            scene.plot_goal = scene_data.value("plot_goal", "");
            scene.emotional_goal = scene_data.value("emotional_goal", "");
            scene.information_goal = scene_data.value("information_goal", "");
            scene.status = worldbuilding::SceneStatus::Draft;
            if (scene_data.contains("suggested_participants") && scene_data["suggested_participants"].is_array()) {
                for (const auto& p : scene_data["suggested_participants"])
                    scene.participant_ids.push_back(p.get<std::string>());
            }
            auto created = service_->create_scene(wid, scene);
            scene_ids.push_back(created.id);
            idx++;
        }
        json_response(res, {{"ok", true}, {"scene_ids", scene_ids}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Agent-driven: generate outline ---

void WorldbuildingHttpHandler::handle_start_generate_outline(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto body = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
        int chapter_count = body.value("chapter_count", 8);
        std::string tmpl = body.value("template", "three_act");
        std::string task = "生成故事大纲：" + std::to_string(chapter_count) + "章，模板=" + tmpl
            + "。产出 arcs 数组，每个 arc 含 title、purpose、chapters（每章含 title、pitch、suggested_pov）。";
        start_agent_run(req, res, wid, task, "generated_outline");
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_get_generated_outline(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto result = service_->worlds().get_agent_result(wid, "generated_outline");
        if (!result) {
            error_response(res, "Outline not yet generated", 404, "result_not_found");
            return;
        }
        json_response(res, {{"ok", true}, {"outline", *result}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_apply_generated_outline(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto result = service_->worlds().get_agent_result(wid, "generated_outline");
        if (!result) {
            error_response(res, "No generated outline to apply", 404, "result_not_found");
            return;
        }
        nlohmann::json arcs_data = result->is_array() ? *result : (*result)["arcs"];
        if (!arcs_data.is_array()) {
            error_response(res, "Outline result does not contain arcs array", 400);
            return;
        }
        nlohmann::json arc_ids = nlohmann::json::array();
        nlohmann::json chapter_ids = nlohmann::json::array();
        int chapter_number = 1;
        for (const auto& arc_data : arcs_data) {
            worldbuilding::Arc arc;
            arc.title = arc_data.value("title", "未命名弧");
            arc.purpose = arc_data.value("purpose", "");
            arc.status = "outline";
            auto created_arc = service_->create_arc(wid, arc);
            arc_ids.push_back(created_arc.id);
            if (arc_data.contains("chapters") && arc_data["chapters"].is_array()) {
                for (const auto& ch_data : arc_data["chapters"]) {
                    worldbuilding::Chapter chapter;
                    chapter.title = ch_data.value("title", "未命名章节");
                    chapter.pitch = ch_data.value("pitch", "");
                    chapter.number = chapter_number++;
                    chapter.arc_id = created_arc.id;
                    chapter.status = worldbuilding::ChapterStatus::Outline;
                    auto created_ch = service_->create_chapter(wid, chapter);
                    chapter_ids.push_back(created_ch.id);
                }
            }
        }
        json_response(res, {{"ok", true}, {"arc_ids", arc_ids}, {"chapter_ids", chapter_ids}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

// --- Agent-driven: scene rewrite ---

void WorldbuildingHttpHandler::handle_start_rewrite_scene(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string sid = req.matches[2];
        auto body = req.body.empty() ? nlohmann::json::object() : nlohmann::json::parse(req.body);
        std::string style = body.value("style", "");
        std::string focus = body.value("focus", "");
        std::string task = "重写场景，风格=" + style + "，重点=" + focus
            + "。产出 rewritten_text 和 changes_summary，不修改原场景。";
        start_agent_run(req, res, wid, task, "rewrite_result:" + sid);
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_get_rewrite_result(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string sid = req.matches[2];
        auto result = service_->worlds().get_agent_result(wid, "rewrite_result:" + sid);
        if (!result) {
            error_response(res, "Rewrite result not yet generated", 404, "result_not_found");
            return;
        }
        json_response(res, {{"ok", true}, {"rewrite_result", *result}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_scene_extraction_result(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string sid = req.matches[2];

        auto world = service_->worlds().get_world(wid);
        if (!world) {
            error_response(res, "World not found", 404);
            return;
        }

        auto scene = service_->narrative().get_scene(wid, sid);
        if (!scene) {
            error_response(res, "Scene not found", 404);
            return;
        }

        auto result = service_->worlds().get_agent_result(wid, "extraction_" + sid);
        nlohmann::json candidates = nlohmann::json::array();
        if (result && result->contains("candidates") && (*result)["candidates"].is_array()) {
            for (const auto& c : (*result)["candidates"]) {
                nlohmann::json candidate;
                if (c.contains("relation")) candidate["relation"] = c["relation"];
                if (c.contains("status")) candidate["status"] = c["status"];
                if (c.contains("evidence")) candidate["evidence"] = c["evidence"];
                if (c.contains("change_summary")) candidate["change_summary"] = c["change_summary"];
                candidates.push_back(candidate);
            }
        }
        json_response(res, {{"ok", true}, {"candidates", candidates}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_search_agents(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        worldbuilding::AgentStore::SearchCriteria criteria;
        criteria.q = req.has_param("q") ? req.get_param_value("q") : "";
        criteria.identity = req.has_param("identity") ? req.get_param_value("identity") : "";
        criteria.race = req.has_param("race") ? req.get_param_value("race") : "";
        if (req.has_param("traits") && !req.get_param_value("traits").empty()) {
            std::string traits_str = req.get_param_value("traits");
            std::istringstream iss(traits_str);
            std::string token;
            while (std::getline(iss, token, ',')) {
                if (!token.empty()) criteria.traits.push_back(token);
            }
        }
        auto results = service_->agents().search_agents(wid, criteria);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& a : results) {
            nlohmann::json agent_obj = {
                {"id", a.id},
                {"name", a.name},
                {"display_name", a.display_name},
                {"kind", worldbuilding::to_string(a.kind)}
            };
            arr.push_back(agent_obj);
        }
        json_response(res, {{"ok", true}, {"agents", arr}, {"total", results.size()}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_character_appearances(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string aid = req.matches[2];
        auto agent = service_->agents().get_agent(aid);
        if (!agent || agent->world_id != wid) {
            error_response(res, "Character not found", 404, "agent_not_found");
            return;
        }
        auto appearances = service_->narrative().find_character_appearances(wid, aid);
        // Group scenes by chapter
        std::map<std::string, nlohmann::json> chapter_map;
        std::map<std::string, std::string> chapter_titles;
        std::map<std::string, int> chapter_numbers;
        for (const auto& ch : appearances.chapters) {
            std::string ch_id = ch["id"];
            chapter_titles[ch_id] = ch.value("title", "");
            chapter_numbers[ch_id] = ch.value("scene_count", 0);
            chapter_map[ch_id] = nlohmann::json::array();
        }
        for (const auto& sc : appearances.scenes) {
            std::string ch_id = sc.value("chapter_id", "");
            // C++17: use find() instead of contains()
            if (chapter_map.find(ch_id) != chapter_map.end()) {
                chapter_map[ch_id].push_back({
                    {"scene_id", sc["id"]},
                    {"scene_title", sc["title"]}
                });
            }
        }
        nlohmann::json chapter_list = nlohmann::json::array();
        for (auto& kv : chapter_map) {
            const auto& ch_id = kv.first;
            chapter_list.push_back({
                {"chapter_id", ch_id},
                {"chapter_title", chapter_titles[ch_id]},
                {"chapter_number", chapter_numbers[ch_id]},
                {"scenes", kv.second}
            });
        }
        json_response(res, {{"ok", true}, {"character_id", aid}, {"appearances", chapter_list}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_pipeline_retreat(const httplib::Request& req, httplib::Response& res) {
    std::string world_id = req.matches[1];
    if (!pipeline_mgr_) {
        res.status = 503;
        res.set_content(R"({"error":"pipeline_not_available"})", "application/json");
        return;
    }

    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        error_response(res, "Invalid JSON body", 400, "invalid_json");
        return;
    }

    if (!body.contains("to_phase") || !body["to_phase"].is_string()) {
        error_response(res, "Missing required field: to_phase", 400, "missing_field");
        return;
    }

    std::string to_phase = body["to_phase"];
    auto phase_opt = worldbuilding::creative_phase_from_string(to_phase);
    if (!phase_opt) {
        error_response(res, "Invalid to_phase: " + to_phase, 400, "invalid_phase");
        return;
    }
    pipeline_mgr_->retreat_to_phase(world_id, to_phase);

    // Return state JSON matching pipeline/state handler format
    auto data = pipeline_mgr_->get_view_data(world_id);
    const auto* wf = pipeline_mgr_->get_workflow(data.active_workflow_name);
    const auto* phase_def = wf ? wf->get_phase(data.state.current_phase) : nullptr;

    nlohmann::json response;
    response["ok"] = true;

    nlohmann::json state;
    state["phase"] = worldbuilding::to_string(data.state.current_phase);
    state["label"] = phase_def ? phase_def->label : "";
    state["active_workflow"] = data.active_workflow_name;

    nlohmann::json conds = nlohmann::json::array();
    for (auto& r : data.current_conditions.results) {
        nlohmann::json cj;
        cj["name"] = r.message;
        cj["met"] = r.met;
        if (r.current) cj["current"] = *r.current;
        if (r.target) cj["target"] = *r.target;
        conds.push_back(cj);
    }
    state["conditions"] = conds;
    state["all_conditions_met"] = data.current_conditions.all_met;

    auto next_phases = worldbuilding::allowed_next_phases(data.state.current_phase);
    nlohmann::json next_arr = nlohmann::json::array();
    for (auto& np : next_phases) next_arr.push_back(worldbuilding::to_string(np));
    state["next_allowed"] = next_arr;
    state["allowed_retreat"] = phase_def ? nlohmann::json(phase_def->allowed_retreat) : nlohmann::json::array();

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
    state["recent_history"] = history;

    response["state"] = state;

    json_response(res, response);
}

void WorldbuildingHttpHandler::handle_pipeline_clear_error(const httplib::Request& req, httplib::Response& res) {
    std::string world_id = req.matches[1];
    if (!pipeline_mgr_) {
        res.status = 503;
        res.set_content(R"({"error":"pipeline_not_available"})", "application/json");
        return;
    }

    try {
        pipeline_mgr_->clear_last_error(world_id);
        json_response(res, {{"ok", true}});
    } catch (const std::exception& e) {
        error_response(res, std::string("Failed to clear error: ") + e.what(), 500, "clear_error_failed");
    }
}

// --- Knowledge Graph handlers ---

void WorldbuildingHttpHandler::handle_kg_entities(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        auto* kg = service_->kg_provider();
        if (!kg) {
            res.status = 503;
            res.set_content(R"({"ok":false,"error":{"code":"kg_unavailable","message":"Knowledge Graph not available","retryable":false}})", "application/json");
            return;
        }

        auto entities = kg->list_entities(wid);

        // Optional type filter
        std::optional<merak::kg::EntityType> type_filter;
        if (req.has_param("type")) {
            type_filter = merak::kg::entity_type_from_string(req.get_param_value("type"));
        }

        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : entities) {
            if (type_filter && e.type != *type_filter) continue;
            arr.push_back({
                {"id", e.source_id},
                {"name", e.name},
                {"type", merak::kg::to_string(e.type)}
            });
        }

        json_response(res, {{"ok", true}, {"entities", arr}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_kg_entity_relations(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];
        std::string eid = req.matches[2];

        auto* kg = service_->kg_provider();
        if (!kg) {
            res.status = 503;
            res.set_content(R"({"ok":false,"error":{"code":"kg_unavailable","message":"Knowledge Graph not available","retryable":false}})", "application/json");
            return;
        }

        // Find the entity by source_id
        auto entities = kg->list_entities(wid);
        const merak::kg::GraphEntity* found = nullptr;
        for (const auto& e : entities) {
            if (e.source_id == eid) {
                found = &e;
                break;
            }
        }

        if (!found) {
            error_response(res, "Entity not found", 404, "entity_not_found");
            return;
        }

        // Query subgraph for this entity's relations
        auto subgraph = kg->query_subgraph(wid, {found->name}, merak::kg::QueryFilters{});

        nlohmann::json rel_arr = nlohmann::json::array();
        for (const auto& rel : subgraph.relations) {
            // Determine which side is the target (the other entity)
            bool is_source = (rel.source_name == found->name);
            std::string target_id = is_source ? rel.target_id : rel.source_id;
            std::string target_name = is_source ? rel.target_name : rel.source_name;
            std::string target_type = is_source
                ? merak::kg::to_string(rel.target_type)
                : merak::kg::to_string(rel.source_type);

            rel_arr.push_back({
                {"relation_type", rel.kind_en},
                {"target_id", target_id},
                {"target_name", target_name},
                {"target_type", target_type}
            });
        }

        nlohmann::json entity_obj = {
            {"id", found->source_id},
            {"name", found->name}
        };

        json_response(res, {{"ok", true}, {"entity", entity_obj}, {"relations", rel_arr}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

void WorldbuildingHttpHandler::handle_kg_search(const httplib::Request& req, httplib::Response& res) {
    try {
        std::string wid = req.matches[1];

        // Require 'q' query parameter
        if (!req.has_param("q") || req.get_param_value("q").empty()) {
            error_response(res, "Missing required query parameter: q", 400, "missing_param");
            return;
        }
        std::string query = req.get_param_value("q");

        auto* kg = service_->kg_provider();
        if (!kg) {
            res.status = 503;
            res.set_content(R"({"ok":false,"error":{"code":"kg_unavailable","message":"Knowledge Graph not available","retryable":false}})", "application/json");
            return;
        }

        // Optional type filter
        std::optional<merak::kg::EntityType> type_filter;
        if (req.has_param("type")) {
            type_filter = merak::kg::entity_type_from_string(req.get_param_value("type"));
        }

        auto entities = kg->list_entities(wid);

        // Case-insensitive substring helper
        auto icontains = [](const std::string& haystack, const std::string& needle) -> bool {
            if (needle.empty()) return true;
            auto it = std::search(
                haystack.begin(), haystack.end(),
                needle.begin(), needle.end(),
                [](unsigned char a, unsigned char b) {
                    return std::tolower(a) == std::tolower(b);
                });
            return it != haystack.end();
        };

        nlohmann::json arr = nlohmann::json::array();
        for (const auto& e : entities) {
            if (type_filter && e.type != *type_filter) continue;
            if (!icontains(e.name, query)) continue;
            arr.push_back({
                {"id", e.source_id},
                {"name", e.name},
                {"type", merak::kg::to_string(e.type)}
            });
        }

        json_response(res, {{"ok", true}, {"results", arr}});
    } catch (const std::exception& e) {
        error_response(res, e.what(), 400);
    }
}

} // namespace merak
