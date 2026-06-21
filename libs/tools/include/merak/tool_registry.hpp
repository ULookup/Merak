#pragma once
#include <merak/tool_base.hpp>
#include <merak/config.hpp>
#include <merak/tool_meta.hpp>
#include <merak/result.hpp>
#include <nlohmann/json.hpp>
#include <map>
#include <vector>
#include <string>
#include <future>
#include <memory>

namespace merak {

class McpClient;

class ToolRegistry {
public:
    ToolRegistry() = default;

    void register_tool(std::unique_ptr<Tool> tool);
    void register_all(std::vector<std::unique_ptr<Tool>> tools);
    void register_platform_basics();
    std::future<Result<int, AgentError>> import_from_mcp(
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

    ToolDomain domain_of(const std::string& name) const;

    std::future<ToolResult> execute(const ToolCall& call, ToolExecutionContext context = {});

    bool check_permission(const std::string& tool_name,
        const std::string& permission_mode) const;
    bool requires_approval(const std::string& tool_name) const;
    bool requires_confirmation(const std::string& tool_name) const;
    void set_permission_mode(const std::string& mode) {
        permission_mode_ = mode;
    }

    /// Full ToolSpecs for all pinned tools in this registry.
    std::vector<ToolSpec> pinned_schemas() const;

    /// Keyword search over non-pinned tools' meta name + description.
    std::string search_tools(const std::string& query, size_t max_results = 5) const;

    /// Select a specific tool by name, returning its full ToolSpec as JSON.
    std::string select_tool(const std::string& name) const;

private:
    std::map<std::string, std::unique_ptr<Tool>> tools_;
    std::map<std::string, std::string> source_;
    std::string permission_mode_ = "ask";
};

} // namespace merak
