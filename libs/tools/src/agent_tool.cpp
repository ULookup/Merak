#include <merak/agent_tool.hpp>
#include <merak/execution.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>

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
      "enum": ["spawn", "list", "get_result"],
      "description": "Action: spawn a sub-agent, list available profiles, or get result of a spawned task"
    },
    "agent_id": {
      "type": "string",
      "description": "Sub-agent profile ID to spawn"
    },
    "task": {
      "type": "string",
      "description": "Task description for the sub-agent"
    },
    "task_id": {
      "type": "string",
      "description": "Task ID returned by spawn. Required for get_result."
    }
  },
  "required": ["action"]
})";
    return s;
}

ToolMeta AgentTool::meta() const {
    ToolMeta m;
    m.name = "agent";
    m.description = "Multi-agent: spawn (create sub-agent), get_result, list";
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
                    auto agent_cfg = it->second;
                    auto exec = executor_;

                    auto fut = std::async(std::launch::async,
                        [exec = std::move(exec), agent_cfg = std::move(agent_cfg), task_text]() -> std::string {
                            try {
                                NullRunControl control;
                                return exec(agent_cfg, task_text, control);
                            } catch (const std::exception& e) {
                                spdlog::error("AgentTool: sub-agent failed: {}", e.what());
                                return std::string("Error: ") + e.what();
                            }
                        });

                    std::string task_id = "task_" + std::to_string(
                        std::chrono::steady_clock::now().time_since_epoch().count());

                    {
                        std::lock_guard<std::mutex> lock(tasks_mutex_);
                        if (active_tasks_.size() >= kMaxConcurrentSubAgents) {
                            result.output = R"({"status":"error","message":"Too many concurrent sub-agents"})";
                            result.is_error = true;
                            return result;
                        }
                        active_tasks_[task_id] = std::move(fut);
                    }

                    nlohmann::json out;
                    out["status"] = "ok";
                    out["message"] = "Sub-agent spawned";
                    out["task_id"] = task_id;
                    out["agent_id"] = agent_id;
                    out["task"] = task_text;
                    result.output = out.dump();
                }
            }
            else if (action == "get_result") {
                std::string task_id = json.value("task_id", "");
                std::lock_guard<std::mutex> lock(tasks_mutex_);
                auto it = active_tasks_.find(task_id);
                if (it == active_tasks_.end()) {
                    result.output = R"({"status":"error","message":"Unknown task_id"})";
                    result.is_error = true;
                } else {
                    auto status = it->second.wait_for(std::chrono::milliseconds(100));
                    if (status == std::future_status::ready) {
                        auto output = it->second.get();
                        active_tasks_.erase(it);
                        nlohmann::json out;
                        out["status"] = "ok";
                        out["result"] = output;
                        result.output = out.dump();
                    } else {
                        result.output = R"({"status":"pending","message":"Task still running"})";
                    }
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
