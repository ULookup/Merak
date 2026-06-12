#pragma once

#include <merak/tool_base.hpp>
#include <merak/tool_meta.hpp>
#include <merak/json_rpc_client.hpp>

#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace merak::tools {

class LspTool : public Tool {
public:
    LspTool() = default;
    explicit LspTool(std::shared_ptr<std::mutex> session_mutex)
        : session_mutex_(std::move(session_mutex)) {}

    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override;
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override;
    bool is_concurrent_safe(const ToolCall&) const override { return true; }

private:
    std::shared_ptr<std::mutex> session_mutex_;
};

} // namespace merak::tools
