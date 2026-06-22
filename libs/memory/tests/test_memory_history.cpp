#include <merak/memory_store.hpp>
#include <merak/config.hpp>
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
static Message make_assistant_with_tool(const std::string& text, const std::string& call_id) {
    Message m; m.role = "assistant"; m.content = text;
    ToolCall tc; tc.id = call_id; tc.name = "read_file"; tc.arguments = "{}";
    m.tool_calls.push_back(tc);
    return m;
}
static Message make_tool_result(const std::string& call_id, const std::string& output) {
    Message m; m.role = "tool"; m.content = output; m.tool_call_id = call_id;
    return m;
}

// Builds a MemoryStore with disabled DB (no PostgreSQL needed).
static std::unique_ptr<MemoryStore> make_store() {
    MemoryConfig cfg;
    cfg.enabled = false;
    return std::make_unique<MemoryStore>(cfg, nullptr);
}

static bool starts_with_user(const std::vector<Message>& msgs) {
    return !msgs.empty() && msgs.front().role == "user";
}

static bool no_orphan_tool_at_head(const std::vector<Message>& msgs) {
    if (msgs.empty()) return true;
    if (msgs.front().role != "tool") return true;
    return false;
}

int main() {
    // Test 1: No tool calls — returns last N turns
    {
        auto store = make_store();
        for (int i = 0; i < 5; i++) {
            store->append_message(make_user("u" + std::to_string(i)));
            store->append_message(make_assistant_text("a" + std::to_string(i)));
        }
        auto hist = store->recent_history(3);
        assert(hist.size() == 6);
        assert(hist[0].content == "u2");
        assert(hist[5].content == "a4");
        std::cout << "Test 1 passed: no tool calls, last N turns\n";
    }

    // Test 2: With tool calls — starts on user boundary
    {
        auto store = make_store();
        store->append_message(make_user("u1"));
        store->append_message(make_assistant_with_tool("", "call_X"));
        store->append_message(make_tool_result("call_X", "r1"));
        store->append_message(make_user("u2"));
        store->append_message(make_assistant_with_tool("", "call_Y"));
        store->append_message(make_tool_result("call_Y", "r2"));
        store->append_message(make_user("u3"));
        store->append_message(make_assistant_text("a3"));

        auto hist = store->recent_history(2);
        assert(starts_with_user(hist));
        assert(hist[0].content == "u2");
        std::cout << "Test 2 passed: with tool calls, starts on user boundary\n";
    }

    // Test 3: Orphan tool at head — expands to parent assistant
    {
        auto store = make_store();
        store->append_message(make_user("u_old"));
        store->append_message(make_assistant_with_tool("", "call_Z"));
        store->append_message(make_tool_result("call_Z", "rZ"));
        store->append_message(make_user("u_recent"));
        store->append_message(make_assistant_text("a_recent"));

        auto hist = store->recent_history(1);
        assert(no_orphan_tool_at_head(hist));
        assert(hist[0].content == "u_recent");
        std::cout << "Test 3 passed: well-formed rounds, no orphan at head\n";
    }

    // Test 4: Orphan tool at head, parent too far — drops orphan
    {
        auto store = make_store();
        store->append_message(make_assistant_with_tool("", "call_far"));
        for (int i = 0; i < 20; i++) {
            store->append_message(make_assistant_text("filler" + std::to_string(i)));
        }
        store->append_message(make_tool_result("call_far", "orphan_result"));
        store->append_message(make_assistant_text("tail"));

        auto hist = store->recent_history(2);
        assert(no_orphan_tool_at_head(hist));
        std::cout << "Test 4 passed: orphan tool at head with far parent, dropped\n";
    }

    // Test 5: Empty memory — returns empty
    {
        auto store = make_store();
        auto hist = store->recent_history(5);
        assert(hist.empty());
        std::cout << "Test 5 passed: empty memory returns empty\n";
    }

    // Test 6: No user messages — fallback to tail with orphan handling
    {
        auto store = make_store();
        store->append_message(make_assistant_with_tool("", "call_nouser"));
        store->append_message(make_tool_result("call_nouser", "r"));
        store->append_message(make_assistant_text("tail"));
        auto hist = store->recent_history(2);
        assert(no_orphan_tool_at_head(hist));
        std::cout << "Test 6 passed: no user messages, fallback handles orphans\n";
    }

    std::cout << "All MemoryStore recent_history tests passed.\n";
    return 0;
}
