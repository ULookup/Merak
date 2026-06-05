#pragma once

#include <merak/tool_base.hpp>
#include <merak/tool_meta.hpp>

#include <future>
#include <memory>
#include <string>

namespace merak::tools {

class TaskTool : public Tool {
public:
    ToolSpec spec() const override;
    PermissionLevel permission() const override;
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override;
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
};

inline ToolSpec TaskTool::spec() const {
    ToolSpec s;
    s.name = "task";
    s.description = "Durable task list: create, update, list, complete, archive";
    s.source = "builtin";
    s.category = Category::Mutating;
    return s;
}

inline PermissionLevel TaskTool::permission() const { return PermissionLevel::safe; }

inline std::future<ToolResult> TaskTool::execute(ToolCall call, ToolExecutionContext /*context*/) {
    return std::async(std::launch::deferred, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        result.output = "task: not yet implemented";
        result.is_error = true;
        return result;
    });
}

inline std::unique_ptr<Tool> TaskTool::clone() const { return std::make_unique<TaskTool>(); }

} // namespace merak::tools
