#include <merak/tool_registry.hpp>
#include <merak/mcp_client.hpp>
#include <merak/mcp_tool_wrapper.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <expected>

namespace merak {

void ToolRegistry::register_tool(std::unique_ptr<Tool> tool) {
    auto spec = tool->spec();
    std::string name = spec.name;

    if (tools_.count(name)) {
        spdlog::warn("ToolRegistry: replacing existing tool '{}'", name);
        tools_.erase(name);
        source_.erase(name);
    }

    source_[name] = spec.source;
    tools_[name] = std::move(tool);
    spdlog::info("ToolRegistry: registered tool '{}' (source={})", name, spec.source);
}

std::future<std::expected<int, AgentError>> ToolRegistry::import_from_mcp(
    std::shared_ptr<McpClient> client
) {
    return std::async(std::launch::async, [this, client = std::move(client)]()
        -> std::expected<int, AgentError>
    {
        auto result = client->list_tools().get();

        if (!result.has_value()) {
            return std::unexpected(result.error());
        }

        int count = 0;
        for (auto& spec : result.value()) {
            auto wrapper = std::make_unique<McpToolWrapper>(
                spec, client, PermissionLevel::ask);
            register_tool(std::move(wrapper));
            count++;
        }

        spdlog::info("ToolRegistry: imported {} tools from MCP '{}'",
            count, client->server_name());
        return count;
    });
}

std::vector<ToolSpec> ToolRegistry::all_tools() const {
    std::vector<ToolSpec> result;
    for (auto& [name, tool] : tools_) {
        result.push_back(tool->spec());
    }
    return result;
}

nlohmann::json ToolRegistry::all_tools_json() const {
    nlohmann::json arr = nlohmann::json::array();
    for (auto& [name, tool] : tools_) {
        auto spec = tool->spec();
        nlohmann::json item;
        item["type"] = "function";
        item["function"]["name"] = spec.name;
        item["function"]["description"] = spec.description;
        if (!spec.parameters_json.empty()) {
            item["function"]["parameters"] =
                nlohmann::json::parse(spec.parameters_json);
        }
        arr.push_back(item);
    }
    return arr;
}

std::optional<ToolSpec> ToolRegistry::find_spec(const std::string& name) const {
    auto it = tools_.find(name);
    if (it != tools_.end()) {
        return it->second->spec();
    }
    return std::nullopt;
}

std::future<ToolResult> ToolRegistry::execute(
    const ToolCall& call, ToolExecutionContext context) {
    auto it = tools_.find(call.name);
    if (it == tools_.end()) {
        return std::async(std::launch::deferred, [call]() -> ToolResult {
            ToolResult result;
            result.call_id = call.id;
            result.is_error = true;
            result.output = "Tool not found: " + call.name;
            return result;
        });
    }

    if (!check_permission(call.name, permission_mode_)) {
        return std::async(std::launch::deferred, [call, mode = permission_mode_]() -> ToolResult {
            ToolResult result;
            result.call_id = call.id;
            result.is_error = true;
            result.output = "Permission denied for tool: " + call.name
                          + " (mode=" + mode + ")";
            return result;
        });
    }

    return it->second->execute(call, std::move(context));
}

std::future<std::vector<ToolResult>> ToolRegistry::execute_all(
    const std::vector<ToolCall>& calls,
    ExecutionPolicy policy,
    ToolExecutionContext context
) {
    return std::async(std::launch::async, [this, calls, policy, context]()
        -> std::vector<ToolResult>
    {
        std::vector<ToolResult> results;

        if (policy == ExecutionPolicy::Parallel) {
            // 分离并发安全和不安全的工具调用
            std::vector<ToolCall> safe_calls, unsafe_calls;
            for (auto& call : calls) {
                auto it = tools_.find(call.name);
                if (it != tools_.end() && it->second->is_concurrent_safe(call)) {
                    safe_calls.push_back(call);
                } else {
                    unsafe_calls.push_back(call);
                }
            }

            // 并发安全的并行执行
            std::vector<std::future<ToolResult>> futures;
            for (auto& call : safe_calls) {
                futures.push_back(execute(call, context));
            }
            for (auto& f : futures) {
                results.push_back(f.get());
            }

            // 不安全的串行执行
            for (auto& call : unsafe_calls) {
                auto result = execute(call, context).get();
                results.push_back(result);
            }
        } else {
            for (auto& call : calls) {
                auto result = execute(call, context).get();
                results.push_back(result);
                if (policy == ExecutionPolicy::FailFast && result.is_error) {
                    break;
                }
            }
        }

        return results;
    });
}

bool ToolRegistry::check_permission(
    const std::string& tool_name,
    const std::string& permission_mode
) const {
    auto it = tools_.find(tool_name);
    if (it == tools_.end()) return false;

    auto perm = it->second->permission();

    if (perm == PermissionLevel::deny) return false;
    if (perm == PermissionLevel::safe) return true;

    if (perm == PermissionLevel::ask) {
        if (permission_mode == "auto" || permission_mode == "bypass") return true;
        if (permission_mode == "deny") return false;
        return true;
    }

    return false;
}

bool ToolRegistry::requires_approval(const std::string& tool_name) const {
    auto it = tools_.find(tool_name);
    if (it == tools_.end()) return false;
    return it->second->permission() == PermissionLevel::ask
        && permission_mode_ == "ask";
}

} // namespace merak
