#pragma once
#include <merak/tool_base.hpp>
#include <merak/config.hpp>
#include <nlohmann/json.hpp>
#include <map>
#include <vector>
#include <string>
#include <expected>
#include <future>
#include <memory>

namespace merak {

class McpClient;

enum class ExecutionPolicy {
    Sequential,
    Parallel,
    FailFast
};

class ToolRegistry {
public:
    ToolRegistry() = default;

    void register_tool(std::unique_ptr<Tool> tool);
    std::future<std::expected<int, AgentError>> import_from_mcp(
        std::shared_ptr<McpClient> client
    );

    std::vector<ToolSpec> all_tools() const;
    nlohmann::json all_tools_json() const;
    std::optional<ToolSpec> find_spec(const std::string& name) const;
    Tool* get_tool(const std::string& name) {
        auto it = tools_.find(name);
        return it != tools_.end() ? it->second.get() : nullptr;
    }
    size_t size() const { return tools_.size(); }

    std::future<ToolResult> execute(const ToolCall& call, ToolExecutionContext context = {});
    std::future<std::vector<ToolResult>> execute_all(
        const std::vector<ToolCall>& calls,
        ExecutionPolicy policy = ExecutionPolicy::Sequential,
        ToolExecutionContext context = {}
    );

    bool check_permission(const std::string& tool_name,
        const std::string& permission_mode) const;
    bool requires_approval(const std::string& tool_name) const;
    void set_permission_mode(const std::string& mode) {
        permission_mode_ = mode;
    }

private:
    std::map<std::string, std::unique_ptr<Tool>> tools_;
    std::map<std::string, std::string> source_;
    std::string permission_mode_ = "ask";
};

} // namespace merak
