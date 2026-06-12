#pragma once

#include <merak/tool_base.hpp>
#include <merak/tool_meta.hpp>
#include <merak/memory_store.hpp>
#include <merak/edit_journal.hpp>

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
    SessionTool(
        std::shared_ptr<MemoryStore> memory,
        std::shared_ptr<merak::Compactor> compactor,
        merak::EditJournal* edit_journal = nullptr)
        : memory_(std::move(memory)), compactor_(std::move(compactor)),
          edit_journal_(edit_journal) {}

    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override;
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override;
    bool is_concurrent_safe(const ToolCall&) const override { return false; }

private:
    std::shared_ptr<MemoryStore> memory_;
    std::shared_ptr<merak::Compactor> compactor_;
    merak::EditJournal* edit_journal_ = nullptr;
};

} // namespace merak::tools
