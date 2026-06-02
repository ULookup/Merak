#pragma once
#include <merak/tool_base.hpp>
#include <merak/mcp_client.hpp>
#include <memory>

namespace merak {

class McpToolWrapper : public Tool {
public:
    McpToolWrapper(ToolSpec spec,
        std::shared_ptr<McpClient> client,
        PermissionLevel perm = PermissionLevel::ask)
        : spec_(std::move(spec))
        , client_(std::move(client))
        , permission_(perm)
    {}

    ToolSpec spec() const override { return spec_; }
    PermissionLevel permission() const override { return permission_; }

    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override {
        return std::async(std::launch::async, [this, call = std::move(call), context]() {
            auto result = client_->call_tool(call).get();
            if (context.cancellation && context.cancellation->cancelled()) {
                result.is_error = true;
                result.output = "Tool result discarded because the run was cancelled";
            }
            return result;
        });
    }

    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<McpToolWrapper>(spec_, client_, permission_);
    }
    bool is_concurrent_safe(const ToolCall&) const override { return false; }

private:
    ToolSpec spec_;
    std::shared_ptr<McpClient> client_;
    PermissionLevel permission_;
};

} // namespace merak
