#pragma once

#include <merak/tool_base.hpp>
#include <merak/tool_meta.hpp>

#include <atomic>
#include <future>
#include <memory>
#include <string>

namespace merak::tools {

class EnterPlanModeTool : public Tool {
public:
    explicit EnterPlanModeTool(std::shared_ptr<std::atomic<bool>> plan_mode);
    ToolSpec spec() const override;
    PermissionLevel permission() const override;
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override;
    bool is_concurrent_safe(const ToolCall&) const override { return false; }

private:
    std::shared_ptr<std::atomic<bool>> plan_mode_;
};

class ExitPlanModeTool : public Tool {
public:
    explicit ExitPlanModeTool(std::shared_ptr<std::atomic<bool>> plan_mode);
    ToolSpec spec() const override;
    PermissionLevel permission() const override;
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override;
    bool is_concurrent_safe(const ToolCall&) const override { return false; }

private:
    std::shared_ptr<std::atomic<bool>> plan_mode_;
};

} // namespace merak::tools
