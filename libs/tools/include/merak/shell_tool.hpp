#pragma once
#include <merak/tool_base.hpp>
#include <string>

namespace merak::tools {

class BashTool : public Tool {
public:
    ToolSpec spec() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override {
        return std::make_unique<BashTool>(*this);
    }
    bool is_concurrent_safe(const ToolCall& call) const override;

private:
    static bool check_dangerous(const std::string& command);
    static bool is_safe_readonly(const std::string& command);
};

} // namespace merak::tools
