#pragma once
#include <merak/message.hpp>
#include <merak/config.hpp>
#include <merak/llm_provider.hpp>
#include <merak/tool_registry.hpp>
#include <merak/memory_store.hpp>
#include <merak/context_assembler.hpp>
#include <merak/compactor.hpp>
#include <map>
#include <string>
#include <vector>
#include <future>
#include <memory>
#include <optional>
#include <shared_mutex>

namespace merak {

namespace worldbuilding { class WorldbuildingService; }
namespace skills { class SkillRegistry; }

struct Delegation {
    std::string agent_id;
    std::string task;
};

class SubAgentRunner : public std::enable_shared_from_this<SubAgentRunner> {
public:
    SubAgentRunner(
        std::shared_ptr<LlmProvider> llm,
        std::shared_ptr<MemoryStore> memory,
        std::shared_ptr<ToolRegistry> parent_tools,
        std::shared_ptr<worldbuilding::WorldbuildingService> worldbuilding = nullptr,
        std::shared_ptr<skills::SkillRegistry> skill_registry = nullptr,
        std::shared_ptr<EmbeddingProvider> embedder = nullptr,
        MemoryConfig memory_config = {}
    );

    void register_profile(const SubAgentConfig& config);
    void set_worldbuilding_service(std::shared_ptr<worldbuilding::WorldbuildingService> worldbuilding);
    void set_skill_registry(std::shared_ptr<skills::SkillRegistry> skill_registry);
    void set_active_world_id(std::optional<std::string> world_id);
    void set_active_scene_id(std::optional<std::string> scene_id);
    void set_caller_agent_id(std::optional<std::string> caller_agent_id);
    std::future<AgentResponse> delegate(
        const std::string& agent_id,
        const std::string& task
    );
    std::future<std::map<std::string, AgentResponse>> fan_out(
        const std::vector<Delegation>& tasks
    );
    std::future<AgentResponse> sequential(
        const std::vector<Delegation>& pipeline
    );
    bool has_agent(const std::string& id) const;

private:
    std::shared_ptr<LlmProvider> llm_;
    std::shared_ptr<MemoryStore> memory_;
    std::shared_ptr<ToolRegistry> parent_tools_;
    std::shared_ptr<worldbuilding::WorldbuildingService> worldbuilding_;
    std::shared_ptr<skills::SkillRegistry> skill_registry_;
    std::shared_ptr<EmbeddingProvider> embedder_;
    MemoryConfig memory_config_;
    std::optional<std::string> active_world_id_;
    std::optional<std::string> active_scene_id_;
    std::optional<std::string> caller_agent_id_;
    std::map<std::string, SubAgentConfig> profiles_;
    mutable std::shared_mutex profiles_mutex_;

    std::unique_ptr<class AgentLoop> create_sub_agent(
        const SubAgentConfig& profile
    );
};

} // namespace merak
