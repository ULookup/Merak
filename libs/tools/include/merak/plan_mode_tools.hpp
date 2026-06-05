#pragma once

#include <merak/tool_base.hpp>
#include <merak/tool_meta.hpp>

#include <future>
#include <memory>
#include <string>

namespace merak::tools {

class EnterPlanModeTool : public Tool {
public:
    ToolSpec spec() const override;
    PermissionLevel permission() const override;
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override;
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
};

inline ToolSpec EnterPlanModeTool::spec() const {
    ToolSpec s;
    s.name = "enter_plan_mode";
    s.description = "Enter plan-authoring mode. Write operations restricted while planning.";
    s.source = "builtin";
    s.category = Category::Mutating;
    return s;
}

inline PermissionLevel EnterPlanModeTool::permission() const { return PermissionLevel::safe; }

inline std::future<ToolResult> EnterPlanModeTool::execute(ToolCall call, ToolExecutionContext /*context*/) {
    return std::async(std::launch::deferred, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        result.output = "enter_plan_mode: not yet implemented";
        result.is_error = true;
        return result;
    });
}

inline std::unique_ptr<Tool> EnterPlanModeTool::clone() const { return std::make_unique<EnterPlanModeTool>(); }

class ExitPlanModeTool : public Tool {
public:
    ToolSpec spec() const override;
    PermissionLevel permission() const override;
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override;
    bool is_concurrent_safe(const ToolCall&) const override { return false; }
};

inline ToolSpec ExitPlanModeTool::spec() const {
    ToolSpec s;
    s.name = "exit_plan_mode";
    s.description = "Submit the authored plan for user review and exit plan-authoring mode.";
    s.source = "builtin";
    s.category = Category::Mutating;
    return s;
}

inline PermissionLevel ExitPlanModeTool::permission() const { return PermissionLevel::safe; }

inline std::future<ToolResult> ExitPlanModeTool::execute(ToolCall call, ToolExecutionContext /*context*/) {
    return std::async(std::launch::deferred, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        result.output = "exit_plan_mode: not yet implemented";
        result.is_error = true;
        return result;
    });
}

inline std::unique_ptr<Tool> ExitPlanModeTool::clone() const { return std::make_unique<ExitPlanModeTool>(); }

} // namespace merak::tools
