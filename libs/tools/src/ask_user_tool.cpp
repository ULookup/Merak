#include <merak/ask_user_tool.hpp>

#include <nlohmann/json.hpp>

#include <future>
#include <string>

namespace merak::tools {

ToolSpec AskUserTool::spec() const {
    ToolSpec s;
    s.name = "ask_user";
    s.description = "Ask the user interactive questions for clarification or confirmation";
    s.source = "builtin";
    s.category = Category::Consultative;
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "question": {
                "type": "string",
                "description": "The question to ask the user"
            },
            "options": {
                "type": "array",
                "items": {
                    "type": "string"
                },
                "description": "Up to 9 multiple-choice options",
                "maxItems": 9
            },
            "multi_select": {
                "type": "boolean",
                "description": "Allow selecting multiple options",
                "default": false
            }
        },
        "required": ["question"]
    })";
    return s;
}

PermissionLevel AskUserTool::permission() const {
    return PermissionLevel::safe;
}

std::future<ToolResult> AskUserTool::execute(
    ToolCall call, ToolExecutionContext /*context*/) {

    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string question = args.value("question", "");

            nlohmann::json output;
            output["status"] = "pending";
            output["question"] = question;
            output["options"] = args.value("options", nlohmann::json::array());
            output["multi_select"] = args.value("multi_select", false);
            output["call_id"] = call.id;

            result.output = output.dump();
        } catch (const std::exception& e) {
            result.output = nlohmann::json{{"status", "error"}, {"message", std::string("AskUser error: ") + e.what()}}.dump();
            result.is_error = true;
        }

        return result;
    });
}

std::unique_ptr<Tool> AskUserTool::clone() const {
    return std::make_unique<AskUserTool>();
}

} // namespace merak::tools
