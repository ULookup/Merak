#include <merak/agent_tool.hpp>
#include <merak/execution.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <thread>

namespace merak::tools {

ToolSpec AgentTool::spec() const {
    ToolSpec s;
    s.name = "agent";
    s.description = "Multi-agent: spawn (create sub-agent), list available agents";
    s.source = "builtin";
    s.category = Category::Mutating;
    s.parameters_json = R"({
  "type": "object",
  "properties": {
    "action": {
      "type": "string",
      "enum": ["spawn", "list"],
      "description": "Action: spawn a sub-agent or list available profiles"
    },
    "agent_id": {
      "type": "string",
      "description": "Sub-agent profile ID to spawn"
    },
    "task": {
      "type": "string",
      "description": "Task description for the sub-agent"
    }
  },
  "required": ["action"]
})";
    return s;
}

ToolMeta AgentTool::meta() const {
    ToolMeta m;
    m.name = "agent";
    m.description = "Multi-agent: spawn (create sub-agent), get_result, send_message, list";
    m.triggers = {"agent", "spawn", "orchestrate", "sub-agent"};
    m.pinned = false;
    m.intents = {IntentType::AgentOp};
    m.scope = Scope::External;
    m.schema_tokens = 40;
    m.domain = ToolDomain::General;
    return m;
}

PermissionLevel AgentTool::permission() const { return PermissionLevel::ask; }

std::unique_ptr<Tool> AgentTool::clone() const {
    return std::make_unique<AgentTool>(profiles_, executor_);
}

std::future<ToolResult> AgentTool::execute(ToolCall call, ToolExecutionContext) {
    return std::async(std::launch::async, [call = std::move(call), this]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto json = nlohmann::json::parse(call.arguments);
            std::string action = json.value("action", "");

            if (action == "list") {
                nlohmann::json out;
                auto arr = nlohmann::json::array();
                for (const auto& [id, cfg] : profiles_) {
                    nlohmann::json item;
                    item["id"] = id;
                    item["description"] = cfg.system_prompt.substr(0, std::min<size_t>(120, cfg.system_prompt.size()));
                    arr.push_back(std::move(item));
                }
                out["status"] = "ok";
                out["available_agents"] = std::move(arr);
                result.output = out.dump();
            }
            else if (action == "spawn") {
                std::string agent_id = json.value("agent_id", "");
                std::string task_text = json.value("task", "");

                auto it = profiles_.find(agent_id);
                if (it == profiles_.end()) {
                    nlohmann::json out;
                    out["status"] = "error";
                    out["message"] = "Unknown agent profile: " + agent_id;
                    auto arr = nlohmann::json::array();
                    for (const auto& [id, _] : profiles_) arr.push_back(id);
                    out["available"] = std::move(arr);
                    result.output = out.dump();
                    result.is_error = true;
                } else {
                    // Launch sub-agent asynchronously via executor
                    auto agent_cfg = it->second;
                    auto exec = executor_;

                    std::thread([exec = std::move(exec), agent_cfg = std::move(agent_cfg), task_text]() {
                        try {
                            NullRunControl control;
                            exec(agent_cfg, task_text, control);
                        } catch (const std::exception& e) {
                            spdlog::error("AgentTool: sub-agent failed: {}", e.what());
                        }
                    }).detach();

                    nlohmann::json out;
                    out["status"] = "ok";
                    out["message"] = "Sub-agent spawned";
                    out["agent_id"] = agent_id;
                    out["task"] = task_text;
                    result.output = out.dump();
                }
            }
            else {
                nlohmann::json out;
                out["status"] = "error";
                out["message"] = "Unknown action: " + action;
                result.output = out.dump();
                result.is_error = true;
            }
        } catch (const std::exception& e) {
            nlohmann::json out;
            out["status"] = "error";
            out["message"] = e.what();
            result.output = out.dump();
            result.is_error = true;
        }

        return result;
    });
}

} // namespace merak::tools
