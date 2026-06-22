#include <merak/context_serializer.hpp>
#include <merak/pipeline_types.hpp>
#include <merak/message.hpp>
#include <merak/tool_spec.hpp>
#include <cassert>
#include <iostream>
#include <string>

using namespace merak;

static Message make_user(const std::string& text) {
    Message m; m.role = "user"; m.content = text; return m;
}
static Message make_assistant_text(const std::string& text) {
    Message m; m.role = "assistant"; m.content = text; return m;
}
static Message make_assistant_with_tool(const std::string& text, const std::string& call_id, const std::string& tool_name = "read_file") {
    Message m; m.role = "assistant"; m.content = text;
    ToolCall tc; tc.id = call_id; tc.name = tool_name; tc.arguments = "{}";
    m.tool_calls.push_back(tc);
    return m;
}
static Message make_tool_result(const std::string& call_id, const std::string& output) {
    Message m; m.role = "tool"; m.content = output; m.tool_call_id = call_id;
    return m;
}

static bool no_orphan_tool_results(const nlohmann::json& msgs) {
    std::vector<std::string> produced_ids;
    for (const auto& m : msgs) {
        const std::string role = m.value("role", "");
        if (m.contains("content") && m["content"].is_array()) {
            for (const auto& blk : m["content"]) {
                if (blk.value("type", "") == "tool_use") {
                    produced_ids.push_back(blk.value("id", ""));
                }
            }
        }
        if (role == "user" && m.contains("content") && m["content"].is_array()) {
            for (const auto& blk : m["content"]) {
                if (blk.value("type", "") == "tool_result") {
                    const std::string use_id = blk.value("tool_use_id", "");
                    bool found = false;
                    for (const auto& pid : produced_ids) {
                        if (pid == use_id) { found = true; break; }
                    }
                    if (!found) return false;
                }
            }
        }
    }
    return true;
}

static bool no_tool_use_at_all(const nlohmann::json& msgs) {
    for (const auto& m : msgs) {
        if (m.contains("content") && m["content"].is_array()) {
            for (const auto& blk : m["content"]) {
                if (blk.value("type", "") == "tool_use") return false;
            }
        }
    }
    return true;
}

int main() {
    ContextSerializer serializer;

    // Test 1: Orphan tool_result at head is dropped (Anthropic)
    {
        BoundContext ctx;
        ctx.provider_messages = {
            make_tool_result("call_orphan_head", "stale result"),
            make_user("hello"),
            make_assistant_text("hi"),
        };
        auto payload = serializer.serialize(ctx, "claude-sonnet-4-6", "", 1024);
        assert(payload.is_anthropic);
        const auto& msgs = payload.anthropic_json["messages"];
        assert(!msgs.empty());
        assert(msgs[0].value("role", "") == "user");
        assert(no_orphan_tool_results(msgs));
        std::cout << "Test 1 passed: orphan tool_result at head dropped (Anthropic)\n";
    }

    // Test 2: Orphan tool_use at tail is dropped (Anthropic)
    {
        BoundContext ctx;
        ctx.provider_messages = {
            make_user("do X"),
            make_assistant_with_tool("", "call_orphan_tail"),
        };
        auto payload = serializer.serialize(ctx, "claude-sonnet-4-6", "", 1024);
        const auto& msgs = payload.anthropic_json["messages"];
        assert(no_tool_use_at_all(msgs));
        std::cout << "Test 2 passed: orphan tool_use at tail dropped (Anthropic)\n";
    }

    // Test 3: Paired tool_use preserved
    {
        BoundContext ctx;
        ctx.provider_messages = {
            make_user("do X"),
            make_assistant_with_tool("", "call_ok"),
            make_tool_result("call_ok", "result"),
        };
        auto payload = serializer.serialize(ctx, "claude-sonnet-4-6", "", 1024);
        const auto& msgs = payload.anthropic_json["messages"];
        assert(no_orphan_tool_results(msgs));
        assert(msgs.size() == 3);
        std::cout << "Test 3 passed: paired tool_use preserved\n";
    }

    // Test 4: Multiple orphan tool_results at head all dropped
    {
        BoundContext ctx;
        ctx.provider_messages = {
            make_tool_result("orphan_1", "r1"),
            make_tool_result("orphan_2", "r2"),
            make_user("hello"),
            make_assistant_text("hi"),
        };
        auto payload = serializer.serialize(ctx, "claude-sonnet-4-6", "", 1024);
        const auto& msgs = payload.anthropic_json["messages"];
        assert(msgs[0].value("role", "") == "user");
        assert(no_orphan_tool_results(msgs));
        std::cout << "Test 4 passed: multiple orphan tool_results at head all dropped\n";
    }

    // Test 5: OpenAI format also has no orphan tool messages
    {
        BoundContext ctx;
        ctx.provider_messages = {
            make_tool_result("call_orphan_openai", "stale"),
            make_user("hello"),
            make_assistant_text("hi"),
        };
        auto payload = serializer.serialize(ctx, "gpt-4o", "", 1024);
        assert(!payload.is_anthropic);
        const auto& msgs = payload.openai_json["messages"];
        bool found_first_non_system = false;
        for (const auto& m : msgs) {
            if (m.value("role", "") == "system") continue;
            assert(m.value("role", "") != "tool");
            found_first_non_system = true;
            break;
        }
        assert(found_first_non_system);
        std::cout << "Test 5 passed: OpenAI format also has no orphan tool at head\n";
    }

    std::cout << "All ContextSerializer orphan tests passed.\n";
    return 0;
}
