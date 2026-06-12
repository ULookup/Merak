#include <merak/plan_mode_tools.hpp>

#include <nlohmann/json.hpp>

#include <future>
#include <memory>
#include <string>

namespace merak::tools {

// --- EnterPlanModeTool ---

EnterPlanModeTool::EnterPlanModeTool(std::shared_ptr<std::atomic<bool>> plan_mode)
    : plan_mode_(std::move(plan_mode)) {}

ToolSpec EnterPlanModeTool::spec() const {
    ToolSpec s;
    s.name = "enter_plan_mode";
    s.description = "Enter plan-authoring mode. Write operations restricted while planning.";
    s.source = "builtin";
    s.category = Category::Mutating;
    s.parameters_json = R"({
        "type": "object",
        "properties": {},
        "required": []
    })";
    return s;
}

PermissionLevel EnterPlanModeTool::permission() const {
    return PermissionLevel::safe;
}

std::future<ToolResult> EnterPlanModeTool::execute(
    ToolCall call, ToolExecutionContext /*context*/) {

    return std::async(std::launch::async, [call = std::move(call), plan_mode = plan_mode_]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            *plan_mode = true;

            nlohmann::json output;
            output["status"] = "ok";
            output["message"] = "Plan mode activated. Write operations are now restricted.";
            output["plan_mode"] = true;
            result.output = output.dump();
        } catch (const std::exception& e) {
            result.output = std::string("Error: ") + e.what();
            result.is_error = true;
        }

        return result;
    });
}

std::unique_ptr<Tool> EnterPlanModeTool::clone() const {
    return std::make_unique<EnterPlanModeTool>(plan_mode_);
}

// --- ExitPlanModeTool ---

ExitPlanModeTool::ExitPlanModeTool(std::shared_ptr<std::atomic<bool>> plan_mode)
    : plan_mode_(std::move(plan_mode)) {}

ToolSpec ExitPlanModeTool::spec() const {
    ToolSpec s;
    s.name = "exit_plan_mode";
    s.description = "Submit the authored plan for user review and exit plan-authoring mode.";
    s.source = "builtin";
    s.category = Category::Mutating;
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "plan": {
                "type": "string",
                "description": "The plan text to submit for review"
            }
        },
        "required": []
    })";
    return s;
}

PermissionLevel ExitPlanModeTool::permission() const {
    return PermissionLevel::safe;
}

std::future<ToolResult> ExitPlanModeTool::execute(
    ToolCall call, ToolExecutionContext /*context*/) {

    return std::async(std::launch::async, [call = std::move(call), plan_mode = plan_mode_]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            *plan_mode = false;

            auto args = nlohmann::json::parse(call.arguments);
            auto plan_text = args.value("plan", "");

            nlohmann::json output;
            output["status"] = "ok";
            output["message"] = "Plan mode deactivated. Plan submitted for review.";
            output["plan_mode"] = false;
            output["plan"] = plan_text;
            result.output = output.dump();
        } catch (const std::exception& e) {
            result.output = std::string("Error: ") + e.what();
            result.is_error = true;
        }

        return result;
    });
}

std::unique_ptr<Tool> ExitPlanModeTool::clone() const {
    return std::make_unique<ExitPlanModeTool>(plan_mode_);
}

} // namespace merak::tools
