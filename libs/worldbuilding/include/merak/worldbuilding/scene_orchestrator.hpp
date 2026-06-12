#pragma once

#include <merak/worldbuilding/agent_store.hpp>
#include <merak/worldbuilding/foreshadowing_store.hpp>
#include <merak/worldbuilding/narrative_store.hpp>
#include <merak/worldbuilding/secret_store.hpp>
#include <merak/worldbuilding/voice_analyzer.hpp>
#include <merak/worldbuilding/world_store.hpp>
#include <merak/tool_spec.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace merak::kg { class KnowledgeGraphProvider; }

namespace merak::worldbuilding {

class WorldbuildingService;

struct CharacterContextView {
    std::string agent_id;
    std::string system_prompt;
    std::vector<std::string> loaded_memory_refs;
};

struct ScenePreparation {
    std::string god_context;
    std::vector<CharacterContextView> character_views;
    std::vector<Foreshadowing> relevant_foreshadowing;
    std::vector<KnowledgeView> secret_views;
    std::map<std::string, std::vector<ToolSpec>> tools_by_agent_id;
};

struct SceneWrapUp {
    std::vector<std::string> pending_diary_agents;  // agent IDs that need to write diary
    std::vector<RelationEntry> relations_updated;
    std::vector<Foreshadowing> proposed_foreshadowing;
    std::vector<LeakRisk> leak_risks;
    ForeshadowStats chapter_foreshadow_stats;
};

class SceneOrchestrator {
public:
    SceneOrchestrator(WorldStore& worlds,
                      AgentStore& agents,
                      NarrativeStore& narrative,
                      ForeshadowingStore& foreshadowing,
                      SecretStore& secrets,
                      VoiceAnalyzer& voice,
                      merak::kg::KnowledgeGraphProvider* kg_provider = nullptr);

    ScenePreparation prepare_scene(const std::string& world_id,
                                    const std::string& scene_id,
                                    WorldbuildingService& service) const;
    SceneWrapUp finish_scene(const std::string& world_id,
                              const std::string& scene_id,
                              const std::string& final_markdown);
    CharacterContextView
    route_direct_message(const std::string& world_id,
                          const std::string& target_agent_id,
                          const std::string& message) const;

private:
    WorldStore& worlds_;
    AgentStore& agents_;
    NarrativeStore& narrative_;
    ForeshadowingStore& foreshadowing_;
    SecretStore& secrets_;
    VoiceAnalyzer& voice_;
    merak::kg::KnowledgeGraphProvider* kg_provider_ = nullptr;
};

} // namespace merak::worldbuilding
