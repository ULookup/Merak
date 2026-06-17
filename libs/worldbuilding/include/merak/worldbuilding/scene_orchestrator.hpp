#pragma once

#include <merak/worldbuilding/agent_store.hpp>
#include <merak/worldbuilding/foreshadowing_store.hpp>
#include <merak/worldbuilding/narrative_store.hpp>
#include <merak/worldbuilding/secret_store.hpp>
#include <merak/worldbuilding/voice_analyzer.hpp>
#include <merak/worldbuilding/world_store.hpp>
#include <merak/tool_spec.hpp>

#include <filesystem>
#include <future>
#include <map>
#include <string>
#include <vector>

#include <merak/llm_provider.hpp>
#include <merak/token_counter.hpp>

namespace merak::kg { class KnowledgeGraphProvider; }

namespace merak::worldbuilding {

struct DiaryCompactionResult {
    bool compressed = false;
    std::string summary_id;
};

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
    std::map<std::string, std::string> behavior_constraints;  // agent_id / kind_name -> prompt
};

struct SceneWrapUp {
    std::vector<std::string> pending_diary_agents;  // agent IDs that need to write diary
    std::vector<RelationEntry> relations_updated;
    std::vector<Foreshadowing> proposed_foreshadowing;
    std::vector<LeakRisk> leak_risks;
    ForeshadowStats chapter_foreshadow_stats;
    std::vector<std::string> compressed_memories;  // 新增: summary_id from auto-compression
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

    void set_prompts_dir(std::filesystem::path path) { prompts_dir_ = std::move(path); }

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

    // 新增: 注入压缩依赖
    void set_compaction_trigger_threshold(int n) { compression_trigger_threshold_ = n; }
    void set_compaction_model(std::string m) { compaction_model_ = std::move(m); }
    void set_compaction_llm(std::shared_ptr<LlmProvider> llm) { compaction_llm_ = std::move(llm); }
    void set_token_counter(std::shared_ptr<TokenCounter> tc) { token_counter_ = std::move(tc); }

private:
    WorldStore& worlds_;
    AgentStore& agents_;
    NarrativeStore& narrative_;
    ForeshadowingStore& foreshadowing_;
    SecretStore& secrets_;
    VoiceAnalyzer& voice_;
    merak::kg::KnowledgeGraphProvider* kg_provider_ = nullptr;
    std::filesystem::path prompts_dir_ = "config/prompts";

    // 新增: 自动压缩配置
    int compression_trigger_threshold_ = 5;
    std::shared_ptr<LlmProvider> compaction_llm_;
    std::shared_ptr<TokenCounter> token_counter_;
    std::string compaction_model_;

    std::future<DiaryCompactionResult> compact_agent_diaries(const std::string& agent_id);
};

} // namespace merak::worldbuilding
