#pragma once
#include <merak/tool_base.hpp>
#include <string>
#include <map>
#include <mutex>
#include <chrono>

namespace merak::tools {

class BashTool : public Tool {
public:
    ToolSpec spec() const override;
    ToolMeta meta() const override;
    PermissionLevel permission() const override { return PermissionLevel::ask; }
    std::future<ToolResult> execute(ToolCall call, ToolExecutionContext context = {}) override;
    std::unique_ptr<Tool> clone() const override {
        // Note: manually clone cache fields since std::mutex is non-copyable
        auto cloned = std::make_unique<BashTool>();
        cloned->readonly_cache_ = readonly_cache_;
        // cache_mutex_ is intentionally not copied — each instance has its own
        return cloned;
    }
    bool is_concurrent_safe(const ToolCall& call) const override;

private:
    struct CacheEntry {
        std::string output;
        int exit_code;
        std::chrono::steady_clock::time_point timestamp;
    };

    static bool is_safe_readonly(const std::string& command);

    std::map<std::string, CacheEntry> readonly_cache_;
    mutable std::mutex cache_mutex_;
};

} // namespace merak::tools
