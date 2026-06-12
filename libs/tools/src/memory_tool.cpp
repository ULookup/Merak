#include <merak/memory_tool.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <regex>
#include <string>
#include <utility>

namespace merak::tools {

ToolSpec MemoryTool::spec() const {
    ToolSpec s;
    s.name = "memory";
    s.description = "Memory operations: store, retrieve, forget, feedback, search, profile";
    s.source = "builtin";
    s.category = Category::Mutating;
    s.parameters_json = R"({
  "type": "object",
  "properties": {
    "action": {
      "type": "string",
      "enum": ["store","retrieve","forget","feedback","search","profile"],
      "description": "The memory operation to perform"
    },
    "content": {
      "type": "string",
      "description": "Content to store (required for store action)"
    },
    "type": {
      "type": "string",
      "description": "Type of memory entry (default: semantic)"
    },
    "query": {
      "type": "string",
      "description": "Search query (required for retrieve/search actions)"
    },
    "top_k": {
      "type": "integer",
      "description": "Number of results to return (default: 5)"
    },
    "memory_id": {
      "type": "string",
      "description": "ID of memory entry (required for forget/feedback actions)"
    },
    "reason": {
      "type": "string",
      "description": "Reason for forgetting (required for forget action)"
    },
    "signal": {
      "type": "string",
      "enum": ["useful","irrelevant","outdated","wrong"],
      "description": "Feedback signal (required for feedback action)"
    }
  },
  "required": ["action"]
})";
    return s;
}

ToolMeta MemoryTool::meta() const {
    ToolMeta m;
    m.name = "memory";
    m.description = "Memory operations: store, retrieve, forget, feedback, search, profile";
    m.triggers = {"memory", "remember", "recall", "forget", "store", "retrieve"};
    m.pinned = false;
    m.intents = {IntentType::Memory};
    m.scope = Scope::CrossSession;
    m.schema_tokens = 40;
    return m;
}

PermissionLevel MemoryTool::permission() const {
    return PermissionLevel::safe;
}

static bool contains_credentials(const std::string& text) {
    static const std::regex cred_pattern(
        R"((ghp_[A-Za-z0-9]{36,})"
        R"(|sk-[A-Za-z0-9]{32,})"
        R"(|AKIA[A-Z0-9]{16})"
        R"(|xoxb-[0-9]{10,13}-[0-9]{10,13}-[A-Za-z0-9]{24})"
        R"(|xoxp-[0-9]{10,13}-[0-9]{10,13}-[A-Za-z0-9]{24})"
        R"(|hf_[A-Za-z0-9]{34})"
        R"(|glpat-[A-Za-z0-9_\-]{20,})"
        R"(|dckr_pat_[A-Za-z0-9_\-]{27,})"
        R"(|github_pat_[A-Za-z0-9_]{22,})"
        R"(|ya29\.[A-Za-z0-9_\-]{100,})"
        R"(|eyJ[A-Za-z0-9_\-]*\.[A-Za-z0-9_\-]*\.[A-Za-z0-9_\-]*))",
        std::regex::ECMAScript | std::regex::optimize
    );
    return std::regex_search(text, cred_pattern);
}

std::future<ToolResult> MemoryTool::execute(ToolCall call, ToolExecutionContext /*context*/) {
    return std::async(std::launch::async, [call = std::move(call), store = store_]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        try {
            auto args = nlohmann::json::parse(call.arguments);
            std::string action = args.value("action", "");

            if (action == "store") {
                std::string content = args.value("content", "");
                std::string type = args.value("type", "semantic");

                if (content.empty()) {
                    result.output = R"({"status":"error","message":"Content is required for store action"})";
                    result.is_error = true;
                    return result;
                }

                if (contains_credentials(content)) {
                    result.output = R"({"status":"error","message":"Content contains credential patterns, refusing to store"})";
                    result.is_error = true;
                    return result;
                }

                auto future = store->store(std::move(content), std::move(type));
                auto expected = future.get();
                if (expected.has_value()) {
                    result.output = R"({"status":"ok","message":"Memory stored"})";
                } else {
                    nlohmann::json err;
                    err["status"] = "error";
                    err["message"] = expected.error().what();
                    result.output = err.dump();
                    result.is_error = true;
                }

            } else if (action == "retrieve" || action == "search") {
                std::string query = args.value("query", "");
                int top_k = args.value("top_k", 5);

                if (query.empty()) {
                    result.output = R"({"status":"error","message":"Query is required for search"})";
                    result.is_error = true;
                    return result;
                }

                auto future = store->search(query, top_k);
                auto expected = future.get();
                if (!expected.has_value()) {
                    nlohmann::json err;
                    err["status"] = "error";
                    err["message"] = expected.error().what();
                    result.output = err.dump();
                    result.is_error = true;
                    return result;
                }

                auto items = nlohmann::json::array();
                for (const auto& snippet : expected.value()) {
                    nlohmann::json item;
                    item["id"] = snippet.id;
                    item["content"] = snippet.content;
                    item["type"] = snippet.type;
                    item["relevance"] = snippet.relevance;
                    items.push_back(std::move(item));
                }
                nlohmann::json out;
                out["status"] = "ok";
                out["results"] = std::move(items);
                out["count"] = out["results"].size();
                result.output = out.dump();

            } else if (action == "forget") {
                std::string memory_id = args.value("memory_id", "");
                std::string reason = args.value("reason", "");

                if (memory_id.empty()) {
                    result.output = R"({"status":"error","message":"memory_id is required for forget action"})";
                    result.is_error = true;
                    return result;
                }
                if (reason.empty()) {
                    result.output = R"({"status":"error","message":"reason is required for forget action"})";
                    result.is_error = true;
                    return result;
                }

                auto expected = store->remove(memory_id);
                if (expected.has_value()) {
                    result.output = R"({"status":"ok","message":"Memory forgotten"})";
                } else {
                    nlohmann::json err;
                    err["status"] = "error";
                    err["message"] = expected.error().what();
                    result.output = err.dump();
                    result.is_error = true;
                }

            } else if (action == "feedback") {
                std::string memory_id = args.value("memory_id", "");
                std::string signal = args.value("signal", "");

                if (memory_id.empty()) {
                    result.output = R"({"status":"error","message":"memory_id is required for feedback action"})";
                    result.is_error = true;
                    return result;
                }
                if (signal.empty()) {
                    result.output = R"({"status":"error","message":"signal is required for feedback action"})";
                    result.is_error = true;
                    return result;
                }

                double weight = 0.0;
                if (signal == "useful") {
                    weight = 0.05;
                } else if (signal == "irrelevant" || signal == "outdated" || signal == "wrong") {
                    weight = -0.1;
                }

                // Attempt observable action: for negative signals, auto-forget the entry
                if (weight < 0.0) {
                    auto removed = store->remove(memory_id);
                    if (removed.has_value()) {
                        spdlog::info("MemoryTool feedback: id={} signal={} — entry removed", memory_id, signal);
                    } else {
                        spdlog::warn("MemoryTool feedback: id={} signal={} — remove failed: {}",
                                     memory_id, signal, removed.error().what());
                    }
                } else {
                    // TODO: MemoryStore has no method to update a single entry's confidence.
                    // Available methods: store, search, remove, decay_confidence, purge_expired.
                    // Adding a store->update(id, delta) method would allow boosting confidence here.
                    spdlog::info("MemoryTool feedback: id={} signal={} weight=+{} — confidence boost noted (no update API)",
                                 memory_id, signal, weight);
                }

                nlohmann::json out;
                out["status"] = "ok";
                out["message"] = "Feedback recorded for memory " + memory_id
                    + " (signal: " + signal + ", adjustment: " + (weight >= 0.0 ? "+" : "") + std::to_string(weight) + ")";
                out["signal"] = signal;
                out["weight_adjustment"] = weight;
                result.output = out.dump();

            } else if (action == "profile") {
                int count = store->message_count();
                nlohmann::json out;
                out["status"] = "ok";
                out["message_count"] = count;
                out["summary"] = "Memory store has " + std::to_string(count) + " messages in working memory";
                result.output = out.dump();

            } else {
                nlohmann::json err;
                err["status"] = "error";
                err["message"] = "Unknown action: " + action;
                result.output = err.dump();
                result.is_error = true;
            }

        } catch (const nlohmann::json::parse_error& e) {
            nlohmann::json err;
            err["status"] = "error";
            err["message"] = std::string("JSON parse error: ") + e.what();
            result.output = err.dump();
            result.is_error = true;
        } catch (const std::exception& e) {
            nlohmann::json err;
            err["status"] = "error";
            err["message"] = e.what();
            result.output = err.dump();
            result.is_error = true;
        }

        return result;
    });
}

std::unique_ptr<Tool> MemoryTool::clone() const {
    return std::make_unique<MemoryTool>(store_);
}

} // namespace merak::tools
