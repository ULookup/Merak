#pragma once

#include <merak/tool_base.hpp>
#include <merak/tool_meta.hpp>

#include <future>
#include <memory>
#include <string>

namespace merak::tools {

class LspTool : public Tool {
public:
    ToolSpec spec() const override;
    PermissionLevel permission() const override;
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override;
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
};

inline ToolSpec LspTool::spec() const {
    ToolSpec s;
    s.name = "lsp";
    s.description = "Language Server Protocol: go_to_definition, find_references, hover, rename, code_action, diagnostics, formatting";
    s.source = "builtin";
    s.category = Category::Consultative;
    return s;
}

inline PermissionLevel LspTool::permission() const { return PermissionLevel::safe; }

inline std::future<ToolResult> LspTool::execute(ToolCall call, ToolExecutionContext /*context*/) {
    return std::async(std::launch::deferred, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        result.output = "lsp: not yet implemented";
        result.is_error = true;
        return result;
    });
}

inline std::unique_ptr<Tool> LspTool::clone() const { return std::make_unique<LspTool>(); }

} // namespace merak::tools
