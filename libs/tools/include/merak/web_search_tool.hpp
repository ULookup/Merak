#pragma once

#include <merak/tool_base.hpp>
#include <merak/tool_meta.hpp>

#include <future>
#include <memory>
#include <string>

namespace merak::tools {

class WebSearchTool : public Tool {
public:
    ToolSpec spec() const override;
    PermissionLevel permission() const override;
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override;
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
};

inline ToolSpec WebSearchTool::spec() const {
    ToolSpec s;
    s.name = "web_search";
    s.description = "Search the web across multiple engines";
    s.source = "builtin";
    s.category = Category::Consultative;
    return s;
}

inline PermissionLevel WebSearchTool::permission() const { return PermissionLevel::ask; }

inline std::future<ToolResult> WebSearchTool::execute(ToolCall call, ToolExecutionContext /*context*/) {
    return std::async(std::launch::deferred, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        result.output = "web_search: not yet implemented";
        result.is_error = true;
        return result;
    });
}

inline std::unique_ptr<Tool> WebSearchTool::clone() const { return std::make_unique<WebSearchTool>(); }

} // namespace merak::tools
