#include <merak/worldbuilding/worldbuilding_service.hpp>

#include <stdexcept>
#include <string>

namespace merak::worldbuilding {

WorldbuildingService::WorldbuildingService(std::string_view pg_conninfo,
                                           std::filesystem::path root)
    : root_(std::move(root)),
      worlds_(pg_conninfo, root_),
      agents_(worlds_, pg_conninfo, root_),
      narrative_(worlds_, root_),
      foreshadowing_(worlds_, narrative_, root_),
      secrets_(worlds_, foreshadowing_, root_),
      voice_(),
      orchestrator_(worlds_, agents_, narrative_, foreshadowing_, secrets_, voice_) {}

void WorldbuildingService::initialize() {
    worlds_.initialize();
}

WorldMeta WorldbuildingService::create_world(std::string name,
                                              std::string description) {
    return worlds_.create_world(std::move(name), std::move(description));
}

std::vector<WorldMeta> WorldbuildingService::list_worlds() const {
    return worlds_.list_worlds();
}

AgentRecord
WorldbuildingService::create_character(const std::string& world_id,
                                        CharacterCard card) {
    return agents_.create_character(world_id, std::move(card));
}

AgentRecord
WorldbuildingService::create_manager(const std::string& world_id,
                                      AgentKind kind,
                                      std::string name,
                                      std::string instructions) {
    return agents_.create_manager(world_id, kind, std::move(name),
                                   std::move(instructions));
}

AgentRecord
WorldbuildingService::create_group(const std::string& world_id,
                                    std::string name,
                                    std::string culture_card,
                                    std::vector<std::string> members) {
    return agents_.create_group(world_id, std::move(name),
                                 std::move(culture_card), std::move(members));
}

Chapter WorldbuildingService::create_chapter(const std::string& world_id,
                                              Chapter chapter) {
    return narrative_.create_chapter(world_id, std::move(chapter));
}

Arc WorldbuildingService::create_arc(const std::string& world_id, Arc arc) {
    return narrative_.create_arc(world_id, std::move(arc));
}

Scene WorldbuildingService::create_scene(const std::string& world_id,
                                          Scene scene) {
    return narrative_.create_scene(world_id, std::move(scene));
}

ScenePreparation
WorldbuildingService::prepare_scene(const std::string& world_id,
                                     const std::string& scene_id) {
    return orchestrator_.prepare_scene(world_id, scene_id, *this);
}

SceneWrapUp WorldbuildingService::end_scene(const std::string& world_id,
                                              const std::string& scene_id,
                                              const std::string& final_markdown) {
    return orchestrator_.finish_scene(world_id, scene_id, final_markdown);
}

Foreshadowing
WorldbuildingService::plant_foreshadowing(const std::string& world_id,
                                           Foreshadowing item) {
    return foreshadowing_.plant(world_id, std::move(item));
}

Secret WorldbuildingService::create_secret(const std::string& world_id,
                                            Secret secret) {
    return secrets_.create(world_id, std::move(secret));
}

std::vector<VoiceComparison>
WorldbuildingService::voice_check(const std::string& world_id) const {
    return voice_.check_all(voice_.list_fingerprints());
}

} // namespace merak::worldbuilding
