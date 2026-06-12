#pragma once

#include <merak/tool_base.hpp>
#include <merak/tool_meta.hpp>
#include <merak/memory_store.hpp>

#include <future>
#include <memory>
#include <string>

namespace merak {
class Compactor;
class CompactionResult;
}

namespace merak::tools {

class SessionTool : public Tool {
public:
    explicit SessionTool(
        std::shared_ptr<MemoryStore> memory,
        std::shared_ptr<merak::Compactor> compactor)
        : memory_(std::move(memory)), compactor_(std::move(compactor)) {}

    ToolSpec spec() const override;
    PermissionLevel permission() const override;
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override;
    bool is_concurrent_safe(const ToolCall&) const override { return false; }

private:
    std::shared_ptr<MemoryStore> memory_;
    std::shared_ptr<merak::Compactor> compactor_;
};

} // namespace merak::tools
