#pragma once
#include <merak/tool_base.hpp>
#include <string>

namespace merak::tools {

class GlobTool : public Tool {
public:
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<GlobTool>(*this);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
};

class GrepTool : public Tool {
public:
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::safe; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<GrepTool>(*this);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
};

} // namespace merak::tools
