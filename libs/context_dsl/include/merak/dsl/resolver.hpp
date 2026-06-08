#pragma once

#include <merak/dsl/parser.hpp>
#include <merak/worldbuilding/worldbuilding_service.hpp>

#include <string>
#include <vector>

namespace merak::dsl {

struct ResolvedContent {
    std::string ref_raw;  // original @xxx{...}
    std::string rendered; // formatted content
};

class Resolver {
    worldbuilding::WorldbuildingService& svc_;
    std::string world_id_;
    std::string scene_id_;
    std::string chapter_id_;
    std::string arc_id_;
    std::string agent_id_;

public:
    Resolver(worldbuilding::WorldbuildingService& svc, std::string world_id)
        : svc_(svc), world_id_(std::move(world_id)) {}

    void set_context(const std::string& scene_id, const std::string& chapter_id,
                     const std::string& arc_id, const std::string& agent_id = "") {
        scene_id_ = scene_id;
        chapter_id_ = chapter_id;
        arc_id_ = arc_id;
        agent_id_ = agent_id;
    }

    ResolvedContent resolve(const DslRef& ref);
};

} // namespace merak::dsl
