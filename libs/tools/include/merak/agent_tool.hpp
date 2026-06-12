#pragma once
#include <merak/tool_base.hpp>
#include <merak/config.hpp>
#include <memory>
#include <functional>
#include <future>
#include <map>
#include <string>

namespace merak { class RunControl; }

namespace merak::tools {

class AgentTool : public Tool {
public:
    using SubExecutor = std::function<std::string(
        const SubAgentConfig& agent, const std::string& task, RunControl& control)>;

    AgentTool(std::map<std::string, SubAgentConfig> profiles,
              SubExecutor executor)
        : profiles_(std::move(profiles)), executor_(std::move(executor)) {}

    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override;
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override;
    bool is_concurrent_safe(const ToolCall&) const override { return false; }

private:
    std::map<std::string, SubAgentConfig> profiles_;
    SubExecutor executor_;
};

} // namespace merak::tools
