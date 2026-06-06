#pragma once

#include <merak/worldbuilding/agent_store.hpp>
#include <merak/worldbuilding/foreshadowing_store.hpp>
#include <merak/worldbuilding/narrative_store.hpp>
#include <merak/worldbuilding/scene_orchestrator.hpp>
#include <merak/worldbuilding/secret_store.hpp>
#include <merak/worldbuilding/voice_analyzer.hpp>
#include <merak/worldbuilding/world_store.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace merak::worldbuilding {

class WorldbuildingService {
public:
    WorldbuildingService(std::string_view pg_conninfo, std::filesystem::path root);
    void initialize();

    // World
    WorldMeta create_world(std::string name, std::string description);
    WorldMeta update_world(const std::string& world_id,
                           const std::optional<std::string>& name,
                           const std::optional<std::string>& description);
    std::vector<WorldMeta> list_worlds() const;

    // Agents
    AgentRecord create_character(const std::string& world_id, CharacterCard card);
    AgentRecord create_manager(const std::string& world_id, AgentKind kind,
                                std::string name, std::string instructions);
    AgentRecord create_group(const std::string& world_id, std::string name,
                              std::string culture_card,
                              std::vector<std::string> members);

    // Narrative
    Chapter create_chapter(const std::string& world_id, Chapter chapter);
    Arc create_arc(const std::string& world_id, Arc arc);
    Scene create_scene(const std::string& world_id, Scene scene);

    // Scene orchestration
    ScenePreparation prepare_scene(const std::string& world_id,
                                    const std::string& scene_id);
    SceneWrapUp end_scene(const std::string& world_id,
                           const std::string& scene_id,
                           const std::string& final_markdown);

    // Foreshadowing & Secrets
    Foreshadowing plant_foreshadowing(const std::string& world_id,
                                       Foreshadowing item);
    Secret create_secret(const std::string& world_id, Secret secret);

    // Voice analysis
    std::vector<VoiceComparison> voice_check(const std::string& world_id) const;

    // Agent prompt management
    void update_agent_prompt(const std::string& agent_id, std::string prompt) {
        agents_.update_agent_prompt(agent_id, std::move(prompt));
    }
    std::string load_agent_prompt(const std::string& agent_id) const {
        return agents_.load_agent_prompt(agent_id);
    }

    // Access underlying stores
    WorldStore& worlds() noexcept { return worlds_; }
    AgentStore& agents() noexcept { return agents_; }
    NarrativeStore& narrative() noexcept { return narrative_; }
    ForeshadowingStore& foreshadowing() noexcept { return foreshadowing_; }
    SecretStore& secrets() noexcept { return secrets_; }
    VoiceAnalyzer& voice() noexcept { return voice_; }

private:
    std::filesystem::path root_;
    WorldStore worlds_;
    AgentStore agents_;
    NarrativeStore narrative_;
    ForeshadowingStore foreshadowing_;
    SecretStore secrets_;
    VoiceAnalyzer voice_;
    SceneOrchestrator orchestrator_;
};

} // namespace merak::worldbuilding
