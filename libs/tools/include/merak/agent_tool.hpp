#pragma once

#include <merak/tool_base.hpp>
#include <merak/tool_meta.hpp>

#include <future>
#include <memory>
#include <string>

namespace merak::tools {

class AgentTool : public Tool {
public:
    ToolSpec spec() const override;
    PermissionLevel permission() const override;
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override;
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
};

inline ToolSpec AgentTool::spec() const {
    ToolSpec s;
    s.name = "agent";
    s.description = "Multi-agent: spawn (create sub-agent), get_result, send_message, list";
    s.source = "builtin";
    s.category = Category::Mutating;
    return s;
}

inline PermissionLevel AgentTool::permission() const { return PermissionLevel::ask; }

inline std::future<ToolResult> AgentTool::execute(ToolCall call, ToolExecutionContext /*context*/) {
    return std::async(std::launch::deferred, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        result.output = "agent: not yet implemented";
        result.is_error = true;
        return result;
    });
}

inline std::unique_ptr<Tool> AgentTool::clone() const { return std::make_unique<AgentTool>(); }

} // namespace merak::tools
