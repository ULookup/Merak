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

namespace merak {

struct Delegation {
    std::string agent_id;
    std::string task;
};

class SubAgentRunner {
public:
    SubAgentRunner(
        std::shared_ptr<LlmProvider> llm,
        std::shared_ptr<MemoryStore> memory,
        std::shared_ptr<ToolRegistry> parent_tools
    );

    void register_profile(const SubAgentConfig& config);
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
    bool has_agent(const std::string& id) const {
        return profiles_.count(id) > 0;
    }

private:
    std::shared_ptr<LlmProvider> llm_;
    std::shared_ptr<MemoryStore> memory_;
    std::shared_ptr<ToolRegistry> parent_tools_;
    std::map<std::string, SubAgentConfig> profiles_;

    std::unique_ptr<class AgentLoop> create_sub_agent(
        const SubAgentConfig& profile
    );
};

} // namespace merak
