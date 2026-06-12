#include <merak/session_tool.hpp>

#include <nlohmann/json.hpp>

#include <future>
#include <memory>
#include <string>

namespace merak::tools {

ToolSpec SessionTool::spec() const {
    ToolSpec s;
    s.name = "session";
    s.description = "Session lifecycle: compact, rollback, config, history, summary, timeline";
    s.source = "builtin";
    s.category = Category::Mutating;
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "action": {
                "type": "string",
                "enum": ["compact", "rollback", "config", "history", "summary", "timeline"],
                "description": "Session action to perform"
            }
        },
        "required": ["action"]
    })";
    return s;
}

PermissionLevel SessionTool::permission() const {
    return PermissionLevel::safe;
}

std::future<ToolResult> SessionTool::execute(
    ToolCall call, ToolExecutionContext /*context*/) {

    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            auto action = args.value("action", "");

            nlohmann::json output;
            output["status"] = "ok";

            if (action == "compact") {
                output["message"] = "Compaction requested";
            } else if (action == "rollback") {
                output["message"] = "Rollback requested";
            } else if (action == "config") {
                output["config"] = nlohmann::json::object();
            } else if (action == "history") {
                output["message"] = "History requested";
            } else if (action == "summary") {
                output["message"] = "Summary requested";
            } else if (action == "timeline") {
                output["message"] = "Timeline requested";
            } else {
                output["status"] = "error";
                output["message"] = "Unknown action: " + action;
                result.is_error = true;
            }

            result.output = output.dump();
        } catch (const std::exception& e) {
            result.output = std::string("Error: ") + e.what();
            result.is_error = true;
        }

        return result;
    });
}

std::unique_ptr<Tool> SessionTool::clone() const {
    return std::make_unique<SessionTool>();
}

} // namespace merak::tools
