#pragma once

#include <merak/tool_base.hpp>
#include <merak/tool_meta.hpp>

#include <future>
#include <memory>
#include <string>

namespace merak::tools {

class SymbolsTool : public Tool {
public:
    ToolSpec spec() const override;
    PermissionLevel permission() const override;
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override;
    bool is_concurrent_safe(const ToolCall&) const override { return true; }
};

inline ToolSpec SymbolsTool::spec() const {
    ToolSpec s;
    s.name = "symbols";
    s.description = "Extract function/class/struct signatures from a file using tree-sitter";
    s.source = "builtin";
    s.category = Category::ReadOnly;
    return s;
}

inline PermissionLevel SymbolsTool::permission() const { return PermissionLevel::safe; }

inline std::future<ToolResult> SymbolsTool::execute(ToolCall call, ToolExecutionContext /*context*/) {
    return std::async(std::launch::deferred, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;
        result.output = "symbols: not yet implemented";
        result.is_error = true;
        return result;
    });
}

inline std::unique_ptr<Tool> SymbolsTool::clone() const { return std::make_unique<SymbolsTool>(); }

} // namespace merak::tools
