#include <merak/sub_agent_runner.hpp>
#include <merak/agent_loop.hpp>
#include <spdlog/spdlog.h>

namespace merak {

SubAgentRunner::SubAgentRunner(
    std::shared_ptr<LlmProvider> llm,
    std::shared_ptr<MemoryStore> memory,
    std::shared_ptr<ToolRegistry> parent_tools,
    std::shared_ptr<worldbuilding::WorldbuildingService> worldbuilding,
    std::shared_ptr<skills::SkillRegistry> skill_registry,
    std::shared_ptr<EmbeddingProvider> embedder,
    MemoryConfig memory_config)
    : llm_(std::move(llm))
    , memory_(std::move(memory))
    , parent_tools_(std::move(parent_tools))
    , worldbuilding_(std::move(worldbuilding))
    , skill_registry_(std::move(skill_registry))
    , embedder_(std::move(embedder))
    , memory_config_(std::move(memory_config))
{
}

void SubAgentRunner::set_worldbuilding_service(
    std::shared_ptr<worldbuilding::WorldbuildingService> worldbuilding) {
    worldbuilding_ = std::move(worldbuilding);
}

void SubAgentRunner::set_skill_registry(
    std::shared_ptr<skills::SkillRegistry> skill_registry) {
    skill_registry_ = std::move(skill_registry);
}

void SubAgentRunner::set_active_world_id(std::optional<std::string> world_id) {
    active_world_id_ = std::move(world_id);
}

void SubAgentRunner::set_active_scene_id(std::optional<std::string> scene_id) {
    active_scene_id_ = std::move(scene_id);
}

void SubAgentRunner::set_caller_agent_id(std::optional<std::string> caller_agent_id) {
    caller_agent_id_ = std::move(caller_agent_id);
}

void SubAgentRunner::register_profile(const SubAgentConfig& config) {
    std::unique_lock<std::shared_mutex> lock(profiles_mutex_);
    profiles_[config.id] = config;
    spdlog::info("SubAgentRunner: registered agent '{}'",
        config.id);
}

bool SubAgentRunner::has_agent(const std::string& id) const {
    std::shared_lock<std::shared_mutex> lock(profiles_mutex_);
    return profiles_.count(id) > 0;
}

std::future<AgentResponse> SubAgentRunner::delegate(
    const std::string& agent_id,
    const std::string& task
) {
    return std::async(std::launch::async, [self = shared_from_this(), agent_id, task]() -> AgentResponse {
        SubAgentConfig cfg;
        {
            std::shared_lock<std::shared_mutex> lock(self->profiles_mutex_);
            auto it = self->profiles_.find(agent_id);
            if (it == self->profiles_.end()) {
                AgentResponse err;
                err.text = "Agent not found: " + agent_id;
                return err;
            }
            cfg = it->second;  // copy out
        }
        auto sub = self->create_sub_agent(cfg);
        spdlog::info("SubAgentRunner: delegating '{}' to '{}'",
            task.substr(0, 30), agent_id);
        NullRunControl control;
        return sub->run(task, control).get();
    });
}

std::future<std::map<std::string, AgentResponse>> SubAgentRunner::fan_out(
    const std::vector<Delegation>& tasks
) {
    return std::async(std::launch::async, [self = shared_from_this(), tasks]()
        -> std::map<std::string, AgentResponse>
    {
        const int max_parallel = std::max(1, std::min(4,
            static_cast<int>(std::thread::hardware_concurrency())));
        std::map<std::string, AgentResponse> results;

        size_t idx = 0;
        while (idx < tasks.size()) {
            std::vector<std::future<std::pair<std::string, AgentResponse>>> batch;
            for (int i = 0; i < max_parallel && idx < tasks.size(); i++, idx++) {
                batch.push_back(std::async(std::launch::async,
                    [self, d = tasks[idx]]() -> std::pair<std::string, AgentResponse> {
                        auto resp = self->delegate(d.agent_id, d.task).get();
                        return {d.agent_id, resp};
                    }));
            }
            for (auto& f : batch) {
                try {
                    auto result = f.get();
                    results[result.first] = result.second;
                } catch (const std::exception& e) {
                    AgentResponse err;
                    err.text = std::string("Sub-agent error: ") + e.what();
                    spdlog::warn("SubAgentRunner: fan_out task failed: {}", e.what());
                }
            }
        }

        spdlog::info("SubAgentRunner: fan_out {} tasks completed", tasks.size());
        return results;
    });
}

std::future<AgentResponse> SubAgentRunner::sequential(
    const std::vector<Delegation>& pipeline
) {
    return std::async(std::launch::async, [self = shared_from_this(), pipeline]() -> AgentResponse {
        std::string accumulated;
        for (auto& d : pipeline) {
            try {
                auto resp = self->delegate(d.agent_id, d.task).get();
                accumulated += "[" + d.agent_id + "]: " + resp.text + "\n";
            } catch (const std::exception& e) {
                accumulated += "[" + d.agent_id + "]: ERROR: " + std::string(e.what()) + "\n";
                spdlog::warn("SubAgentRunner: sequential step failed: {}", e.what());
            }
        }
        AgentResponse final_resp;
        final_resp.text = accumulated;
        return final_resp;
    });
}

std::unique_ptr<AgentLoop> SubAgentRunner::create_sub_agent(
    const SubAgentConfig& profile
) {
    AgentLoop::Config cfg;
    cfg.system_prompt = profile.system_prompt;
    cfg.max_turns = profile.max_turns > 0 ? profile.max_turns : 10;

    auto sub_tools = std::make_shared<ToolRegistry>();

    if (parent_tools_) {
        if (profile.tool_allowlist.empty()) {
            for (auto& spec : parent_tools_->all_tools()) {
                auto* tool = parent_tools_->get_tool(spec.name);
                if (tool) sub_tools->register_tool(tool->clone());
            }
        } else {
            for (auto& allowed : profile.tool_allowlist) {
                auto* tool = parent_tools_->get_tool(allowed);
                if (tool) sub_tools->register_tool(tool->clone());
            }
        }
    }

    auto counter = std::make_shared<TokenCounter>();
    TokenBudget budget;
    budget.model_max_tokens = 128000;

    auto comp = std::make_shared<Compactor>(llm_, counter);

    auto sub_memory = std::make_shared<MemoryStore>(memory_config_, embedder_);
    auto loop = std::make_unique<AgentLoop>(
        cfg, llm_, sub_tools, sub_memory, comp, worldbuilding_, skill_registry_);
    loop->set_active_world_id(active_world_id_);
    loop->set_active_scene_id(active_scene_id_);
    loop->set_caller_agent_id(caller_agent_id_.value_or(profile.id));
    return loop;
}

} // namespace merak
