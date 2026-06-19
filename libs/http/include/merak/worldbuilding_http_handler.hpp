#pragma once

#include <merak/worldbuilding/worldbuilding_service.hpp>
#include <merak/worldbuilding/pipeline_manager.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <merak/storage/image_service.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace merak {

class RuntimeService;

class WorldbuildingHttpHandler {
public:
    explicit WorldbuildingHttpHandler(
        std::shared_ptr<worldbuilding::WorldbuildingService> service,
        std::shared_ptr<RuntimeService> runtime = nullptr);

    void install_routes(httplib::Server& server);
    void set_pipeline_manager(std::shared_ptr<worldbuilding::PipelineManager> mgr);
    void set_image_service(std::shared_ptr<ImageService> img_svc);

private:
    std::shared_ptr<worldbuilding::WorldbuildingService> service_;
    std::shared_ptr<RuntimeService> runtime_;
    std::shared_ptr<worldbuilding::PipelineManager> pipeline_mgr_;
    std::shared_ptr<ImageService> image_service_;

    // World
    void handle_list_worlds(const httplib::Request&, httplib::Response&);
    void handle_get_world(const httplib::Request&, httplib::Response&);
    void handle_create_world(const httplib::Request&, httplib::Response&);
    void handle_delete_world(const httplib::Request&, httplib::Response&);
    void handle_update_world(const httplib::Request&, httplib::Response&);

    // Agent
    void handle_list_agents(const httplib::Request&, httplib::Response&);
    void handle_search_agents(const httplib::Request&, httplib::Response&);
    void handle_create_agent(const httplib::Request&, httplib::Response&);
    void handle_get_agent(const httplib::Request&, httplib::Response&);
    void handle_delete_agent(const httplib::Request&, httplib::Response&);

    // Narrative
    void handle_overview(const httplib::Request&, httplib::Response&);
    void handle_list_chapters(const httplib::Request&, httplib::Response&);
    void handle_get_chapter(const httplib::Request&, httplib::Response&);
    void handle_chapter_tree(const httplib::Request&, httplib::Response&);
    void handle_list_scenes(const httplib::Request&, httplib::Response&);
    void handle_scene_new(const httplib::Request&, httplib::Response&);
    void handle_scene_end(const httplib::Request&, httplib::Response&);
    void handle_get_scene(const httplib::Request&, httplib::Response&);
    void handle_delete_scene(const httplib::Request&, httplib::Response&);
    void handle_delete_chapter(const httplib::Request&, httplib::Response&);
    void handle_reorder_chapters(const httplib::Request&, httplib::Response&);

    // Time
    void handle_time_now(const httplib::Request&, httplib::Response&);
    void handle_time_advance(const httplib::Request&, httplib::Response&);

    // Foreshadowing
    void handle_foreshadow_list(const httplib::Request&, httplib::Response&);
    void handle_foreshadow_plant(const httplib::Request&, httplib::Response&);
    void handle_delete_foreshadowing(const httplib::Request&, httplib::Response&);

    // Secret
    void handle_secret_list(const httplib::Request&, httplib::Response&);
    void handle_secret_create(const httplib::Request&, httplib::Response&);
    void handle_delete_secret(const httplib::Request&, httplib::Response&);

    // Locations
    void handle_list_locations(const httplib::Request&, httplib::Response&);
    void handle_get_location(const httplib::Request&, httplib::Response&);
    void handle_create_location(const httplib::Request&, httplib::Response&);
    void handle_delete_location(const httplib::Request&, httplib::Response&);

    // Knowledge
    void handle_list_knowledge(const httplib::Request&, httplib::Response&);
    void handle_create_knowledge(const httplib::Request&, httplib::Response&);
    void handle_update_knowledge(const httplib::Request&, httplib::Response&);
    void handle_delete_knowledge(const httplib::Request&, httplib::Response&);

    // Factions
    void handle_list_factions(const httplib::Request&, httplib::Response&);
    void handle_get_faction(const httplib::Request&, httplib::Response&);
    void handle_create_faction(const httplib::Request&, httplib::Response&);
    void handle_update_faction(const httplib::Request&, httplib::Response&);
    void handle_delete_faction(const httplib::Request&, httplib::Response&);

    // Dashboard
    void handle_dashboard(const httplib::Request&, httplib::Response&);

    // File links
    void handle_list_file_links(const httplib::Request&, httplib::Response&);
    void handle_create_file_link(const httplib::Request&, httplib::Response&);
    void handle_delete_file_link(const httplib::Request&, httplib::Response&);

    // Preview builders (Agent-driven)
    void handle_build_preview(const httplib::Request&, httplib::Response&);

    // Pending creations (Agent-driven)
    void handle_store_pending_creation(const httplib::Request&, httplib::Response&);
    void handle_get_pending_creation(const httplib::Request&, httplib::Response&);
    void handle_resolve_creation(const httplib::Request&, httplib::Response&);

    // Agent-driven generation infrastructure
    struct PendingAgentRun {
        std::string world_id;
        std::string operation_type;
    };
    void start_agent_run(const httplib::Request& req, httplib::Response& res,
                         const std::string& world_id, const std::string& task_description,
                         const std::string& operation_type);
    void capture_agent_result(const std::string& run_id);

    // Agent-driven: suggestions
    void handle_start_suggestions(const httplib::Request&, httplib::Response&);
    void handle_get_suggestions(const httplib::Request&, httplib::Response&);

    // Agent-driven: consistency check
    void handle_start_consistency_check(const httplib::Request&, httplib::Response&);
    void handle_get_consistency_check(const httplib::Request&, httplib::Response&);

    // Agent-driven: generate scenes
    void handle_start_generate_scenes(const httplib::Request&, httplib::Response&);
    void handle_get_generated_scenes(const httplib::Request&, httplib::Response&);
    void handle_apply_generated_scenes(const httplib::Request&, httplib::Response&);

    // Agent-driven: generate outline
    void handle_start_generate_outline(const httplib::Request&, httplib::Response&);
    void handle_get_generated_outline(const httplib::Request&, httplib::Response&);
    void handle_apply_generated_outline(const httplib::Request&, httplib::Response&);

    // Agent-driven: scene rewrite
    void handle_start_rewrite_scene(const httplib::Request&, httplib::Response&);

    // Character appearances
    void handle_character_appearances(const httplib::Request&, httplib::Response&);
    void handle_get_rewrite_result(const httplib::Request&, httplib::Response&);

    std::unordered_map<std::string, PendingAgentRun> pending_agent_runs_;
    mutable std::mutex pending_runs_mutex_;

    // Agent prompt
    void handle_load_agent_prompt(const httplib::Request&, httplib::Response&);

    // Agent card & diaries
    void handle_patch_agent(const httplib::Request&, httplib::Response&);
    void handle_agent_diary_list(const httplib::Request&, httplib::Response&);
    void handle_agent_diary_add(const httplib::Request&, httplib::Response&);
    void handle_agent_relations(const httplib::Request&, httplib::Response&);

    // Chapter review
    void handle_chapter_review(const httplib::Request&, httplib::Response&);

    // Export
    void handle_export_chapters(const httplib::Request&, httplib::Response&);

    // PATCH routes for narrative entities
    void handle_patch_scene(const httplib::Request&, httplib::Response&);
    void handle_patch_chapter(const httplib::Request&, httplib::Response&);

    // PATCH routes for world entities
    void handle_patch_foreshadow(const httplib::Request&, httplib::Response&);
    void handle_patch_secret(const httplib::Request&, httplib::Response&);

    // Image routes
    void handle_list_images(const httplib::Request&, httplib::Response&);
    void handle_upload_image(const httplib::Request&, httplib::Response&);
    void handle_delete_image(const httplib::Request&, httplib::Response&);
    void handle_patch_image(const httplib::Request&, httplib::Response&);
    void handle_serve_image(const httplib::Request&, httplib::Response&);
    // Chunked upload
    void handle_init_chunked(const httplib::Request&, httplib::Response&);
    void handle_upload_chunk(const httplib::Request&, httplib::Response&);
    void handle_chunked_status(const httplib::Request&, httplib::Response&);
    void handle_complete_chunked(const httplib::Request&, httplib::Response&);
    void handle_cancel_chunked(const httplib::Request&, httplib::Response&);
};

} // namespace merak
