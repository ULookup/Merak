#pragma once
#include <merak/tool_spec.hpp>
#include <merak/message.hpp>
#include <merak/error.hpp>
#include <string>
#include <future>
#include <memory>

namespace merak {

enum class PermissionLevel { safe, ask, deny };

class Tool {
public:
    virtual ~Tool() = default;
    virtual ToolSpec spec() const = 0;
    virtual PermissionLevel permission() const = 0;
    virtual std::future<ToolResult> execute(ToolCall call) = 0;
    virtual std::unique_ptr<Tool> clone() const = 0;
    virtual bool is_concurrent_safe(const ToolCall& call) const { return false; }
};

} // namespace merak
