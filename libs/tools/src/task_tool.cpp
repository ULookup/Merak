#include <merak/task_tool.hpp>

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

namespace merak::tools {

// --- In-memory task store with mutex protection ---

static std::mutex& task_mutex() {
    static std::mutex mtx;
    return mtx;
}

static std::map<std::string, nlohmann::json>& task_store() {
    static std::map<std::string, nlohmann::json> store;
    return store;
}

static std::atomic<int64_t>& task_counter() {
    static std::atomic<int64_t> counter(0);
    return counter;
}

static std::string generate_task_id() {
    auto count = task_counter().fetch_add(1);
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    std::ostringstream id;
    id << "task_" << now << "_" << count;
    return id.str();
}

static std::string now_iso() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

// --- TaskTool ---

ToolSpec TaskTool::spec() const {
    ToolSpec s;
    s.name = "task";
    s.description = "Durable task list: create, update, list, complete, archive";
    s.source = "builtin";
    s.category = Category::Mutating;
    s.parameters_json = R"({
        "type": "object",
        "properties": {
            "action": {
                "type": "string",
                "enum": ["create", "update", "list", "complete", "archive"],
                "description": "Task action"
            },
            "id": {
                "type": "string",
                "description": "Task ID (required for update, complete, archive)"
            },
            "title": {
                "type": "string",
                "description": "Task title"
            },
            "description": {
                "type": "string",
                "description": "Task description"
            },
            "status": {
                "type": "string",
                "description": "Task status"
            },
            "priority": {
                "type": "string",
                "description": "Task priority"
            }
        },
        "required": ["action"]
    })";
    return s;
}

ToolMeta TaskTool::meta() const {
    ToolMeta m;
    m.name = "task";
    m.description = "Durable task list: create, update, list, complete, archive";
    m.triggers = {"task", "todo", "checklist", "track"};
    m.pinned = false;
    m.intents = {IntentType::TaskMgmt};
    m.scope = Scope::CrossSession;
    m.schema_tokens = 30;
    return m;
}

PermissionLevel TaskTool::permission() const {
    return PermissionLevel::safe;
}

std::future<ToolResult> TaskTool::execute(
    ToolCall call, ToolExecutionContext /*context*/) {

    return std::async(std::launch::async, [call = std::move(call)]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            auto action = args.value("action", "");

            std::lock_guard<std::mutex> lock(task_mutex());
            auto& store = task_store();

            if (action == "create") {
                auto id = generate_task_id();
                nlohmann::json task;
                task["id"] = id;
                task["title"] = args.value("title", "");
                task["description"] = args.value("description", "");
                task["status"] = args.value("status", "pending");
                task["priority"] = args.value("priority", "medium");
                task["created_at"] = now_iso();
                store[id] = task;

                result.output = task.dump();
            } else if (action == "update") {
                auto id = args.value("id", "");
                if (id.empty() || store.find(id) == store.end()) {
                    result.output = R"({"error":"Task not found"})";
                    result.is_error = true;
                    return result;
                }
                auto& task = store[id];
                if (args.contains("title")) task["title"] = args["title"];
                if (args.contains("description")) task["description"] = args["description"];
                if (args.contains("status")) task["status"] = args["status"];
                if (args.contains("priority")) task["priority"] = args["priority"];
                task["updated_at"] = now_iso();
                result.output = task.dump();
            } else if (action == "list") {
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& [id, task] : store) {
                    arr.push_back(task);
                }
                result.output = arr.dump();
            } else if (action == "complete") {
                auto id = args.value("id", "");
                if (id.empty() || store.find(id) == store.end()) {
                    result.output = R"({"error":"Task not found"})";
                    result.is_error = true;
                    return result;
                }
                auto& task = store[id];
                task["status"] = "completed";
                task["completed_at"] = now_iso();
                result.output = task.dump();
            } else if (action == "archive") {
                auto id = args.value("id", "");
                if (id.empty() || store.find(id) == store.end()) {
                    result.output = R"({"error":"Task not found"})";
                    result.is_error = true;
                    return result;
                }
                auto task = store[id];
                store.erase(id);
                nlohmann::json out;
                out["archived"] = task;
                out["message"] = "Task archived";
                result.output = out.dump();
            } else {
                nlohmann::json err;
                err["error"] = "Unknown action: " + action;
                result.output = err.dump();
                result.is_error = true;
            }
        } catch (const std::exception& e) {
            result.output = std::string("Error: ") + e.what();
            result.is_error = true;
        }

        return result;
    });
}

std::unique_ptr<Tool> TaskTool::clone() const {
    return std::make_unique<TaskTool>();
}

} // namespace merak::tools
