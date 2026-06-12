#pragma once

#include <merak/tool_base.hpp>
#include <merak/tool_meta.hpp>

#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace merak::tools {

class AskUserTool : public Tool {
public:
    using AskHandler = std::function<std::string(
        const std::string& question,
        const std::vector<std::string>& options)>;

    explicit AskUserTool(AskHandler handler = nullptr)
        : ask_handler_(std::move(handler)) {}

    ToolSpec spec() const override;
    PermissionLevel permission() const override;
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override;
    bool is_concurrent_safe(const ToolCall&) const override { return false; }

private:
    AskHandler ask_handler_;
};

} // namespace merak::tools
