#include <merak/session_tool.hpp>
#include <merak/compactor.hpp>

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

ToolMeta SessionTool::meta() const {
    ToolMeta m;
    m.name = "session";
    m.description = "Session lifecycle: compact, rollback, config, history, summary, timeline";
    m.triggers = {"compact", "rollback", "config", "adjust"};
    m.pinned = false;
    m.intents = {IntentType::Introspect};
    m.scope = Scope::Local;
    m.schema_tokens = 60;
    m.domain = ToolDomain::General;
    return m;
}

PermissionLevel SessionTool::permission() const {
    return PermissionLevel::safe;
}

std::future<ToolResult> SessionTool::execute(
    ToolCall call, ToolExecutionContext /*context*/) {

    return std::async(std::launch::async, [call = std::move(call), this]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            auto action = args.value("action", "");

            nlohmann::json out;
            out["status"] = "ok";

            if (action == "compact") {
                if (compactor_ && memory_) {
                    auto history = memory_->recent_history(100);
                    auto compact_result = compactor_->compact_history(history, 10).get();
                    out["message"] = "Compaction completed";
                    if (!compact_result.summary.empty()) {
                        out["summary"] = compact_result.summary;
                    }
                    out["tokens_before"] = compact_result.tokens_before;
                    out["tokens_after"] = compact_result.tokens_after;
                } else {
                    out["status"] = "error";
                    out["message"] = "Compactor or MemoryStore not available";
                    result.is_error = true;
                }
            } else if (action == "rollback") {
                if (edit_journal_) {
                    int count = args.value("count", 1);
                    bool ok = edit_journal_->rollback(static_cast<size_t>(count));
                    if (ok) {
                        out["message"] = "Rolled back " + std::to_string(count) + " file modification(s)";
                        out["rolled_back"] = count;
                    } else {
                        out["status"] = "error";
                        out["message"] = "Rollback failed: not enough entries in edit journal";
                        result.is_error = true;
                    }
                } else {
                    out["status"] = "error";
                    out["message"] = "Rollback not available: no EditJournal connected";
                    result.is_error = true;
                }
            } else if (action == "config") {
                out["message"] = "Session configuration";
                nlohmann::json cfg;
                if (memory_) {
                    cfg["message_count"] = memory_->message_count();
                }
                out["config"] = cfg;
            } else if (action == "history") {
                if (memory_) {
                    int n = args.value("n", 20);
                    auto msgs = memory_->recent_history(n);
                    auto arr = nlohmann::json::array();
                    for (const auto& msg : msgs) {
                        nlohmann::json item;
                        item["role"] = msg.role;
                        item["content"] = msg.content.substr(0, std::min<size_t>(200, msg.content.size()));
                        arr.push_back(std::move(item));
                    }
                    out["messages"] = std::move(arr);
                    out["count"] = out["messages"].size();
                } else {
                    out["message"] = "Memory store not available";
                }
            } else if (action == "summary") {
                if (memory_) {
                    int count = memory_->message_count();
                    out["message"] = "Session summary";
                    out["message_count"] = count;
                    out["summary_text"] = "Session has " + std::to_string(count) + " messages in working memory";
                } else {
                    out["message"] = "Memory store not available";
                }
            } else if (action == "timeline") {
                if (memory_) {
                    int n = args.value("n", 20);
                    auto msgs = memory_->recent_history(n);
                    auto items = nlohmann::json::array();
                    int idx = 0;
                    for (const auto& msg : msgs) {
                        nlohmann::json item;
                        item["index"] = idx++;
                        item["role"] = msg.role;
                        item["content"] = msg.content.substr(0, std::min<size_t>(300, msg.content.size()));
                        items.push_back(std::move(item));
                    }
                    out["message"] = "Session timeline";
                    out["total_messages"] = msgs.size();
                    out["timeline_items"] = std::move(items);
                } else {
                    out["message"] = "Memory store not available";
                }
            } else {
                out["status"] = "error";
                out["message"] = "Unknown action: " + action;
                result.is_error = true;
            }

            result.output = out.dump();
        } catch (const std::exception& e) {
            result.output = nlohmann::json{{"status", "error"}, {"message", std::string("Session tool error: ") + e.what()}}.dump();
            result.is_error = true;
        }

        return result;
    });
}

std::unique_ptr<Tool> SessionTool::clone() const {
    return std::make_unique<SessionTool>(memory_, compactor_, edit_journal_);
}

} // namespace merak::tools
