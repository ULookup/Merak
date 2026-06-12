#include <merak/tool_search_tool.hpp>
#include <merak/tool_registry.hpp>

#include <nlohmann/json.hpp>
#include <future>
#include <sstream>

namespace merak::tools {

ToolSpec ToolSearchTool::spec() const {
    ToolSpec s;
    s.name = "tool_search";
    s.description = "Search and activate deferred tools that are not in the static tool list.\n\n"
                    "Two modes:\n"
                    "- Keyword search: {\"query\": \"git log\"} -- returns top matches with name + short description\n"
                    "- Select mode: {\"query\": \"select:tool_name\"} -- returns FULL schema (name + description + parameters) "
                    "so the tool can be invoked immediately\n"
                    "- Multi-select: {\"query\": \"select:tool_a,tool_b\"} -- returns full schemas for multiple tools";
    s.source = "builtin";
    s.category = Category::Consultative;
    s.parameters_json = R"JSON({
        "type": "object",
        "properties": {
            "query": {
                "type": "string",
                "description": "Search query with keywords describing what you want to do, or use select:NAME to get full schema for a specific tool"
            },
            "max_results": {
                "type": "integer",
                "description": "Maximum results for keyword search (default 5, max 20)"
            }
        },
        "required": ["query"]
    })JSON";
    return s;
}

ToolMeta ToolSearchTool::meta() const {
    ToolMeta m;
    m.name = "tool_search";
    m.description = "Search and activate deferred tools. Use select:NAME to get full schema.";
    m.triggers = {"tool_search", "find tool", "which tool", "available tools"};
    m.pinned = true;
    m.intents = {IntentType::CodeRead};
    m.scope = Scope::Local;
    m.schema_tokens = 25;
    return m;
}

PermissionLevel ToolSearchTool::permission() const {
    return PermissionLevel::safe;
}

std::future<ToolResult> ToolSearchTool::execute(
    ToolCall call, ToolExecutionContext /*context*/) {

    return std::async(std::launch::async, [registry = registry_, call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            auto query = args.value("query", "");

            if (query.empty()) {
                result.output = "Error: 'query' is required";
                result.is_error = true;
                return result;
            }

            size_t max_results = args.value("max_results", size_t{5});
            if (max_results > 20) max_results = 20;

            // Select mode: select:name or select:a,b,c
            if (query.starts_with("select:")) {
                auto names_str = query.substr(7);
                std::vector<std::string> names;
                std::istringstream ss(names_str);
                std::string name;
                while (std::getline(ss, name, ',')) {
                    // trim whitespace
                    name.erase(0, name.find_first_not_of(" \t"));
                    name.erase(name.find_last_not_of(" \t") + 1);
                    if (!name.empty()) names.push_back(name);
                }

                nlohmann::json matches = nlohmann::json::array();
                nlohmann::json missing = nlohmann::json::array();
                for (const auto& n : names) {
                    auto schema_json = registry->select_tool(n);
                    auto parsed = nlohmann::json::parse(schema_json);
                    if (parsed.contains("error")) {
                        missing.push_back(n);
                    } else {
                        matches.push_back(parsed);
                    }
                }

                result.output = nlohmann::json{
                    {"query", query},
                    {"matches", matches},
                    {"missing", missing}
                }.dump();
            } else {
                // Keyword search mode
                result.output = registry->search_tools(query, max_results);
            }
        } catch (const std::exception& e) {
            result.output = std::string("Error: ") + e.what();
            result.is_error = true;
        }

        return result;
    });
}

std::unique_ptr<Tool> ToolSearchTool::clone() const {
    return std::make_unique<ToolSearchTool>(registry_);
}

} // namespace merak::tools
