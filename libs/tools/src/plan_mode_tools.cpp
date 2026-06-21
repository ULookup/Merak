#include <merak/plan_mode_tools.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

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

ToolMeta EnterPlanModeTool::meta() const {
    ToolMeta m;
    m.name = "enter_plan_mode";
    m.description = "Enter plan-authoring mode. Write operations restricted while planning.";
    m.triggers = {"plan", "enter plan mode"};
    m.pinned = false;
    m.intents = {IntentType::Introspect};
    m.scope = Scope::Local;
    m.schema_tokens = 15;
    m.domain = ToolDomain::General;
    return m;
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

ExitPlanModeTool::ExitPlanModeTool(std::shared_ptr<std::atomic<bool>> plan_mode,
                                   std::shared_ptr<SessionStore> session_store)
    : plan_mode_(std::move(plan_mode)), session_store_(std::move(session_store)) {}

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

ToolMeta ExitPlanModeTool::meta() const {
    ToolMeta m;
    m.name = "exit_plan_mode";
    m.description = "Submit the authored plan for user review and exit plan-authoring mode.";
    m.triggers = {"exit plan", "submit plan"};
    m.pinned = false;
    m.intents = {IntentType::Introspect};
    m.scope = Scope::Local;
    m.schema_tokens = 20;
    m.domain = ToolDomain::General;
    return m;
}

PermissionLevel ExitPlanModeTool::permission() const {
    return PermissionLevel::safe;
}

std::future<ToolResult> ExitPlanModeTool::execute(
    ToolCall call, ToolExecutionContext /*context*/) {

    return std::async(std::launch::async, [call = std::move(call), this]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            *plan_mode_ = false;

            auto args = nlohmann::json::parse(call.arguments);
            auto plan_text = args.value("plan", "");

            if (session_store_ && !plan_text.empty()) {
                session_store_->set_plan(plan_text);
                spdlog::info("ExitPlanModeTool: saved plan ({} chars) to session store", plan_text.size());
            }

            nlohmann::json output;
            output["status"] = "ok";
            output["message"] = "Plan mode deactivated. Plan saved for review.";
            output["plan_mode"] = false;
            if (!plan_text.empty()) {
                output["plan"] = plan_text;
            }
            result.output = output.dump();
        } catch (const std::exception& e) {
            result.output = std::string("Error: ") + e.what();
            result.is_error = true;
        }

        return result;
    });
}

std::unique_ptr<Tool> ExitPlanModeTool::clone() const {
    return std::make_unique<ExitPlanModeTool>(plan_mode_, session_store_);
}

} // namespace merak::tools
