#include <merak/tool_registry.hpp>
#include <merak/mcp_client.hpp>
#include <merak/mcp_tool_wrapper.hpp>
#include <merak/builtin_tools.hpp>
#include <merak/tool_search_tool.hpp>
#include <merak/shell_tool.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <sstream>

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

void ToolRegistry::register_all(std::vector<std::unique_ptr<Tool>> tools) {
    for (auto& tool : tools) {
        register_tool(std::move(tool));
    }
}

std::future<Result<int, AgentError>> ToolRegistry::import_from_mcp(
    std::shared_ptr<McpClient> client
) {
    return std::async(std::launch::async, [this, client = std::move(client)]()
        -> Result<int, AgentError>
    {
        auto result = client->list_tools().get();

        if (!result.has_value()) {
            return Result<int, AgentError>(std::move(result).error());
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

bool ToolRegistry::requires_confirmation(const std::string& tool_name) const {
    auto it = tools_.find(tool_name);
    if (it == tools_.end()) return false;
    return it->second->spec().requires_confirmation;
}

bool ToolRegistry::requires_approval(const std::string& tool_name) const {
    auto it = tools_.find(tool_name);
    if (it == tools_.end()) return false;
    return it->second->permission() == PermissionLevel::ask
        && permission_mode_ == "ask";
}

namespace {

int match_score(const std::string& query, const std::string& text) {
    int score = 0;
    std::string q_lower = query;
    std::string t_lower = text;
    std::transform(q_lower.begin(), q_lower.end(), q_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::transform(t_lower.begin(), t_lower.end(), t_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (t_lower.find(q_lower) != std::string::npos) {
        score += 10;
    }

    std::istringstream q_stream(q_lower);
    std::string word;
    while (q_stream >> word) {
        if (t_lower.find(word) != std::string::npos) {
            score += 1;
        }
    }

    return score;
}

} // anonymous namespace

std::vector<ToolSpec> ToolRegistry::pinned_schemas() const {
    std::vector<ToolSpec> result;
    for (const auto& [name, tool] : tools_) {
        auto meta = tool->meta();
        if (!meta.pinned) continue;
        result.push_back(tool->spec());
    }
    return result;
}

std::string ToolRegistry::search_tools(const std::string& query, size_t max_results) const {
    struct ScoredMeta {
        ToolMeta meta;
        int score;
    };
    std::vector<ScoredMeta> scored;

    for (const auto& [name, tool] : tools_) {
        ToolMeta meta = tool->meta();
        if (meta.pinned) continue;

        int score = match_score(query, std::string(meta.name));
        score += match_score(query, std::string(meta.description));
        for (const auto& trigger : meta.triggers) {
            score += match_score(query, trigger);
        }
        if (score > 0) {
            scored.push_back({std::move(meta), score});
        }
    }

    std::sort(scored.begin(), scored.end(),
              [](const ScoredMeta& a, const ScoredMeta& b) { return a.score > b.score; });

    if (scored.size() > max_results) {
        scored.resize(max_results);
    }

    nlohmann::json matches = nlohmann::json::array();
    for (const auto& sm : scored) {
        std::string desc(sm.meta.description);
        if (desc.size() > 100) {
            desc = desc.substr(0, 100) + "...";
        }
        matches.push_back({
            {"name", sm.meta.name},
            {"description", desc},
            {"score", sm.score}
        });
    }

    size_t total_deferred = 0;
    for (const auto& [name, tool] : tools_) {
        if (!tool->meta().pinned) total_deferred++;
    }

    return nlohmann::json{
        {"query", query},
        {"matches", matches},
        {"total_tools", total_deferred}
    }.dump();
}

std::string ToolRegistry::select_tool(const std::string& name) const {
    auto it = tools_.find(name);
    if (it == tools_.end()) {
        return nlohmann::json{{"error", "tool not found: " + name}}.dump();
    }

    auto spec = it->second->spec();

    nlohmann::json params = nlohmann::json::object();
    if (!spec.parameters_json.empty()) {
        try {
            params = nlohmann::json::parse(spec.parameters_json);
        } catch (const nlohmann::json::parse_error&) {
            return nlohmann::json{{"error", "invalid parameters JSON for: " + name}}.dump();
        }
    }

    return nlohmann::json{
        {"name", spec.name},
        {"description", spec.description},
        {"parameters", params}
    }.dump();
}

void ToolRegistry::register_platform_basics() {
    register_tool(std::make_unique<tools::ReadFileTool>());
    register_tool(std::make_unique<tools::WriteFileTool>());
    register_tool(std::make_unique<tools::StrReplaceTool>());
    register_tool(std::make_unique<tools::ListDirTool>());
    register_tool(std::make_unique<tools::GlobTool>());
    register_tool(std::make_unique<tools::GrepTool>());
    register_tool(std::make_unique<tools::BashTool>());
}

} // namespace merak
