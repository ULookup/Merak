#pragma once

#include <merak/worldbuilding/worldbuilding_service.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>

namespace merak {

class RuntimeService;

class WorldbuildingHttpHandler {
public:
    explicit WorldbuildingHttpHandler(
        std::shared_ptr<worldbuilding::WorldbuildingService> service,
        std::shared_ptr<RuntimeService> runtime = nullptr);

    void install_routes(httplib::Server& server);

private:
    std::shared_ptr<worldbuilding::WorldbuildingService> service_;
    std::shared_ptr<RuntimeService> runtime_;

    // World
    void handle_list_worlds(const httplib::Request&, httplib::Response&);
    void handle_get_world(const httplib::Request&, httplib::Response&);
    void handle_create_world(const httplib::Request&, httplib::Response&);
    void handle_delete_world(const httplib::Request&, httplib::Response&);
    void handle_update_world(const httplib::Request&, httplib::Response&);

    // Agent
    void handle_list_agents(const httplib::Request&, httplib::Response&);
    void handle_create_agent(const httplib::Request&, httplib::Response&);
    void handle_get_agent(const httplib::Request&, httplib::Response&);

    // Narrative
    void handle_overview(const httplib::Request&, httplib::Response&);
    void handle_list_chapters(const httplib::Request&, httplib::Response&);
    void handle_list_scenes(const httplib::Request&, httplib::Response&);
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

    // Agent prompt
    void handle_load_agent_prompt(const httplib::Request&, httplib::Response&);

    // Agent card & diaries
    void handle_patch_agent(const httplib::Request&, httplib::Response&);
    void handle_agent_diary_list(const httplib::Request&, httplib::Response&);
    void handle_agent_diary_add(const httplib::Request&, httplib::Response&);
    void handle_agent_relations(const httplib::Request&, httplib::Response&);

    // PATCH routes for narrative entities
    void handle_patch_scene(const httplib::Request&, httplib::Response&);
    void handle_patch_chapter(const httplib::Request&, httplib::Response&);

    // PATCH routes for world entities
    void handle_patch_foreshadow(const httplib::Request&, httplib::Response&);
    void handle_patch_secret(const httplib::Request&, httplib::Response&);
};

} // namespace merak
