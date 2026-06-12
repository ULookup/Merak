#pragma once

#include <merak/tool_base.hpp>
#include <merak/memory_store.hpp>

#include <future>
#include <memory>

namespace merak::tools {

class MemoryTool : public Tool {
public:
    explicit MemoryTool(std::shared_ptr<MemoryStore> store)
        : store_(std::move(store)) {}

    ToolSpec spec() const override;
    PermissionLevel permission() const override;
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override;
    bool is_concurrent_safe(const ToolCall&) const override { return true; }

private:
    std::shared_ptr<MemoryStore> store_;
};

} // namespace merak::tools
