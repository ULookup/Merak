#pragma once

#include <merak/tool_base.hpp>

#include <memory>

namespace merak {
class ToolRegistry;  // forward declare in correct namespace
}

namespace merak::tools {

class ToolSearchTool : public Tool {
public:
    explicit ToolSearchTool(std::shared_ptr<merak::ToolRegistry> registry)
        : registry_(std::move(registry)) {}

    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override;
    std::future<ToolResult> execute(
        ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override;
    bool is_concurrent_safe(const ToolCall&) const override { return true; }

private:
    std::shared_ptr<merak::ToolRegistry> registry_;
};

} // namespace merak::tools
