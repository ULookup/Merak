#include <merak/context_pipeline.hpp>
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

static bool all_tool_messages_paired(const std::vector<Message>& msgs) {
    std::vector<std::string> produced;
    for (const auto& m : msgs) {
        if (m.role == "assistant") {
            for (const auto& tc : m.tool_calls) produced.push_back(tc.id);
        }
        if (m.role == "tool") {
            if (!m.tool_call_id) return false;
            bool found = false;
            for (const auto& pid : produced) {
                if (pid == *m.tool_call_id) { found = true; break; }
            }
            if (!found) return false;
        }
    }
    return true;
}

int main() {
    // Test 1: Hard trim keeps round boundaries (no orphan tool_result)
    {
        ContextPipeline pipeline;
        std::vector<Message> history;
        for (int r = 0; r < 5; r++) {
            std::string rid = "r" + std::to_string(r);
            history.push_back(make_user(std::string(2000, 'u') + rid));
            history.push_back(make_assistant_with_tool("", "call_" + rid));
            history.push_back(make_tool_result("call_" + rid, std::string(2000, 't')));
        }
        history.push_back(make_user("finalize"));

        BindSources sources;
        sources.conversation_messages = history;
        auto payload = pipeline.planned_assemble("system", "claude-sonnet-4-6",
                                                  500, history, sources);
        assert(pipeline.stats().hard_trims > 0);
        assert(all_tool_messages_paired(payload.messages));
        assert(!payload.messages.empty());
        assert(payload.messages.front().role == "user");
        std::cout << "Test 1 passed: hard trim keeps round boundaries\n";
    }

    // Test 2: Hard trim preserves at least one round
    {
        ContextPipeline pipeline;
        std::vector<Message> history;
        for (int r = 0; r < 5; r++) {
            std::string rid = "r" + std::to_string(r);
            history.push_back(make_user(std::string(2000, 'u') + rid));
            history.push_back(make_assistant_text(std::string(2000, 'a')));
        }
        BindSources sources;
        sources.conversation_messages = history;
        auto payload = pipeline.planned_assemble("system", "claude-sonnet-4-6",
                                                  10, history, sources);
        assert(pipeline.stats().hard_trims > 0);
        assert(!payload.messages.empty());
        assert(payload.messages.front().role == "user");
        std::cout << "Test 2 passed: hard trim preserves at least one round\n";
    }

    // Test 3: End-to-end — no orphan tool_result in anthropic_json
    {
        ContextPipeline pipeline;
        std::vector<Message> history;
        for (int r = 0; r < 5; r++) {
            std::string rid = "r" + std::to_string(r);
            history.push_back(make_user(std::string(2000, 'u') + rid));
            history.push_back(make_assistant_with_tool("", "call_" + rid));
            history.push_back(make_tool_result("call_" + rid, std::string(2000, 't')));
        }
        history.push_back(make_user("go"));

        BindSources sources;
        sources.conversation_messages = history;
        auto payload = pipeline.planned_assemble("system", "claude-sonnet-4-6",
                                                  500, history, sources);
        assert(pipeline.stats().hard_trims > 0);
        std::vector<std::string> produced_ids;
        const auto& msgs = payload.anthropic_json["messages"];
        bool ok = true;
        for (const auto& m : msgs) {
            if (m.contains("content") && m["content"].is_array()) {
                for (const auto& blk : m["content"]) {
                    const std::string type = blk.value("type", "");
                    if (type == "tool_use") produced_ids.push_back(blk.value("id", ""));
                    if (type == "tool_result") {
                        const std::string use_id = blk.value("tool_use_id", "");
                        bool found = false;
                        for (const auto& pid : produced_ids) {
                            if (pid == use_id) { found = true; break; }
                        }
                        if (!found) { ok = false; break; }
                    }
                }
            }
            if (!ok) break;
        }
        assert(ok);
        std::cout << "Test 3 passed: end-to-end no orphan tool_result (ISSUE #171 repro)\n";
    }

    // Test 4: Hard trim does not delete system messages
    {
        ContextPipeline pipeline;
        std::vector<Message> history;
        // Leading system message
        Message sys; sys.role = "system"; sys.content = "system prompt";
        history.push_back(sys);
        // 3 rounds with large content to force hard trim.
        // Uses <4 rounds so drop_rounds (min_rounds_to_keep=4 default) is a
        // no-op (drop_count <= 0 returns immediately). This isolates the spec
        // behavior under test: hard trim erases from rs[0] (first user index)
        // to rs[1], so a leading system message at index 0 is never touched.
        for (int r = 0; r < 3; r++) {
            std::string rid = "r" + std::to_string(r);
            history.push_back(make_user(std::string(2000, 'u') + rid));
            history.push_back(make_assistant_text(std::string(2000, 'a')));
        }
        BindSources sources;
        sources.conversation_messages = history;
        auto payload = pipeline.planned_assemble("system", "claude-sonnet-4-6",
                                                  500, history, sources);
        assert(pipeline.stats().hard_trims > 0);
        // System message must survive — check payload.messages
        assert(!payload.messages.empty());
        assert(payload.messages.front().role == "system");
        assert(payload.messages.front().content == "system prompt");
        std::cout << "Test 4 passed: hard trim does not delete system messages\n";
    }

    std::cout << "All ContextPipeline hard trim tests passed.\n";
    return 0;
}
