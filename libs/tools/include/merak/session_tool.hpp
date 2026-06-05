#pragma once

#include <merak/tool_base.hpp>
#include <merak/tool_meta.hpp>

#include <future>
#include <memory>
#include <string>

namespace merak::tools {

class SessionTool : public Tool {
public:
    ToolSpec spec() const override;
    PermissionLevel permission() const override;
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override;
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
};

inline ToolSpec SessionTool::spec() const {
    ToolSpec s;
    s.name = "session";
    s.description = "Session lifecycle: compact, rollback, config, history, summary, timeline";
    s.source = "builtin";
    s.category = Category::Mutating;
    return s;
}

inline PermissionLevel SessionTool::permission() const { return PermissionLevel::safe; }

inline std::future<ToolResult> SessionTool::execute(ToolCall call, ToolExecutionContext /*context*/) {
    return std::async(std::launch::deferred, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        result.output = "session: not yet implemented";
        result.is_error = true;
        return result;
    });
}

inline std::unique_ptr<Tool> SessionTool::clone() const { return std::make_unique<SessionTool>(); }

} // namespace merak::tools
