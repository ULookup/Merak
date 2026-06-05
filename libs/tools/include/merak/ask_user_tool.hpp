#pragma once

#include <merak/tool_base.hpp>
#include <merak/tool_meta.hpp>

#include <future>
#include <memory>
#include <string>

namespace merak::tools {

class AskUserTool : public Tool {
public:
    ToolSpec spec() const override;
    PermissionLevel permission() const override;
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override;
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
};

inline ToolSpec AskUserTool::spec() const {
    ToolSpec s;
    s.name = "ask_user";
    s.description = "Ask the user interactive questions for clarification or confirmation";
    s.source = "builtin";
    s.category = Category::Consultative;
    return s;
}

inline PermissionLevel AskUserTool::permission() const { return PermissionLevel::safe; }

inline std::future<ToolResult> AskUserTool::execute(ToolCall call, ToolExecutionContext /*context*/) {
    return std::async(std::launch::deferred, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        result.output = "ask_user: not yet implemented";
        result.is_error = true;
        return result;
    });
}

inline std::unique_ptr<Tool> AskUserTool::clone() const { return std::make_unique<AskUserTool>(); }

} // namespace merak::tools
