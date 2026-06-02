#include <merak/sub_agent_runner.hpp>
#include <merak/agent_loop.hpp>
#include <spdlog/spdlog.h>

namespace merak {

SubAgentRunner::SubAgentRunner(
    std::shared_ptr<LlmProvider> llm,
    std::shared_ptr<MemoryStore> memory,
    std::shared_ptr<ToolRegistry> parent_tools)
    : llm_(std::move(llm))
    , memory_(std::move(memory))
    , parent_tools_(std::move(parent_tools))
{
}

void SubAgentRunner::register_profile(const SubAgentConfig& config) {
    profiles_[config.id] = config;
    spdlog::info("SubAgentRunner: registered agent '{}'",
        config.id);
}

std::future<AgentResponse> SubAgentRunner::delegate(
    const std::string& agent_id,
    const std::string& task
) {
    return std::async(std::launch::async, [this, agent_id, task]() -> AgentResponse {
        auto it = profiles_.find(agent_id);
        if (it == profiles_.end()) {
            AgentResponse err;
            err.text = "Agent not found: " + agent_id;
            return err;
        }

        auto sub = create_sub_agent(it->second);
        spdlog::info("SubAgentRunner: delegating '{}' to '{}'",
            task.substr(0, 30), agent_id);
        NullRunControl control;
        return sub->run(task, control).get();
    });
}

std::future<std::map<std::string, AgentResponse>> SubAgentRunner::fan_out(
    const std::vector<Delegation>& tasks
) {
    return std::async(std::launch::async, [this, tasks]()
        -> std::map<std::string, AgentResponse>
    {
        std::map<std::string, AgentResponse> results;
        std::vector<std::future<std::pair<std::string, AgentResponse>>> futures;

        for (auto& d : tasks) {
            futures.push_back(std::async(std::launch::async,
                [this, d]() -> std::pair<std::string, AgentResponse> {
                    auto resp = delegate(d.agent_id, d.task).get();
                    return {d.agent_id, resp};
                }));
        }

        for (auto& f : futures) {
            auto [id, resp] = f.get();
            results[id] = resp;
        }

        spdlog::info("SubAgentRunner: fan_out {} tasks completed", tasks.size());
        return results;
    });
}

std::future<AgentResponse> SubAgentRunner::sequential(
    const std::vector<Delegation>& pipeline
) {
    return std::async(std::launch::async, [this, pipeline]() -> AgentResponse {
        std::string accumulated;
        for (auto& d : pipeline) {
            auto resp = delegate(d.agent_id, d.task).get();
            accumulated += "[" + d.agent_id + "]: " + resp.text + "\n";
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
    cfg.max_turns = 10;

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

    auto ctx = std::make_shared<ContextAssembler>(budget, counter);
    auto comp = std::make_shared<Compactor>(llm_, counter);

    return std::make_unique<AgentLoop>(cfg, llm_, sub_tools, memory_, ctx, comp);
}

} // namespace merak
