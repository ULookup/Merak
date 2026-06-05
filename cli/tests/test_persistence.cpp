#include "../src/tui/persistence/turn_event.hpp"
#include "../src/tui/persistence/turn_event_json.hpp"
#include "../src/tui/persistence/transcript.hpp"
#include <cassert>
#include <iostream>
#include <cstdio>

using namespace merak::tui::persistence;

static void test_user_event_roundtrip() {
    UserEvent e{"hello world", 1234567890ULL, {{"file1", "abc"}, {"file2", "def"}}};
    auto j = to_json(e);
    assert(j["kind"] == "user");
    assert(j["text"] == "hello world");
    assert(j["ts"] == 1234567890ULL);
    auto e2 = std::get<UserEvent>(from_json(j));
    assert(e2.text == "hello world");
    assert(e2.timestamp_ms == 1234567890ULL);
}

static void test_assistant_event_roundtrip() {
    AssistantEvent e{"code review", "claude-sonnet-4-6", 9876543210ULL, 3};
    auto j = to_json(e);
    assert(j["kind"] == "assistant");
    assert(j["model"] == "claude-sonnet-4-6");
    auto e2 = std::get<AssistantEvent>(from_json(j));
    assert(e2.text == "code review");
    assert(e2.model == "claude-sonnet-4-6");
    assert(e2.frozen_phase == 3);
}

static void test_tool_event_roundtrip() {
    ToolEvent e;
    e.tool_name = "grep";
    e.args = {{"pattern", "main"}, {"path", "src/"}};
    e.output = "src/main.cpp:1:int main()";
    e.exit_code = 0;
    e.elapsed_ms = 42;
    e.status = ToolEvent::Status::success;
    auto j = to_json(e);
    assert(j["kind"] == "tool");
    assert(j["name"] == "grep");
    auto e2 = std::get<ToolEvent>(from_json(j));
    assert(e2.tool_name == "grep");
    assert(e2.output == "src/main.cpp:1:int main()");
    assert(e2.elapsed_ms == 42);
    assert(e2.status == ToolEvent::Status::success);
}

static void test_tool_event_error_roundtrip() {
    ToolEvent e;
    e.tool_name = "rm";
    e.output = "permission denied";
    e.status = ToolEvent::Status::error;
    auto j = to_json(e);
    assert(j["status"] == "error");
    auto e2 = std::get<ToolEvent>(from_json(j));
    assert(e2.status == ToolEvent::Status::error);
}

static void test_system_event_roundtrip() {
    SystemEvent e{"connection lost", SystemEvent::Level::warn, 1111111111ULL};
    auto j = to_json(e);
    assert(j["kind"] == "system");
    assert(j["level"] == "warn");
    auto e2 = std::get<SystemEvent>(from_json(j));
    assert(e2.level == SystemEvent::Level::warn);
}

static void test_turn_summary_roundtrip() {
    TurnSummaryEvent e{1500U, 800U, 0.015, 3200ULL, 3U};
    auto j = to_json(e);
    assert(j["kind"] == "turn_summary");
    assert(j["tokens_in"] == 1500U);
    assert(j["cost"] == 0.015);
    auto e2 = std::get<TurnSummaryEvent>(from_json(j));
    assert(e2.tokens_in == 1500U);
    assert(e2.tokens_out == 800U);
}

static void test_approval_event_roundtrip() {
    ApprovalEvent e{"bash", "rm -rf /tmp/cache", ApprovalEvent::Decision::denied, 2222222222ULL};
    auto j = to_json(e);
    assert(j["kind"] == "approval");
    assert(j["decision"] == "denied");
    auto e2 = std::get<ApprovalEvent>(from_json(j));
    assert(e2.decision == ApprovalEvent::Decision::denied);
}

static void test_session_meta_roundtrip() {
    SessionMeta e{"sess_001", 9999999999ULL, "claude-opus-4-7", "anthropic",
                   "Prompt", "abc123", "/home/user/project", uint16_t{120}, uint16_t{40}, "1.2.3"};
    auto j = to_json(e);
    assert(j["kind"] == "session_meta");
    assert(j["sid"] == "sess_001");
    assert(j["version"] == "1.2.3");
    auto e2 = std::get<SessionMeta>(from_json(j));
    assert(e2.session_id == "sess_001");
    assert(e2.terminal_w == 120);
    assert(e2.terminal_h == 40);
}

static void test_variant_roundtrip() {
    TurnEvent original = UserEvent{"test", 0ULL, {}};
    auto j = to_json(original);
    auto restored = from_json(j);
    assert(std::holds_alternative<UserEvent>(restored));
}

static void test_unknown_kind_fallback() {
    auto j = nlohmann::json::parse(R"({"kind":"bogus","text":"x"})");
    auto e = from_json(j);
    assert(std::holds_alternative<UserEvent>(e));
}

static void test_transcript_append_read() {
    const std::string sid = "test_transcript_001";
    append_event(sid, UserEvent{"hello", 1000ULL, {}});
    append_event(sid, AssistantEvent{"hi there", "test-model", 2000ULL, 0});
    auto events = read_events(sid);
    assert(events.size() == 2);
    assert(std::holds_alternative<UserEvent>(events[0]));
    assert(std::holds_alternative<AssistantEvent>(events[1]));
    delete_session(sid);
    assert(read_events(sid).empty());
}

static void test_transcript_empty_session() {
    auto events = read_events("nonexistent_session_xyz");
    assert(events.empty());
}

static void test_transcript_skips_malformed() {
    const std::string sid = "test_malformed_001";
    append_event(sid, UserEvent{"valid", 0ULL, {}});
    auto path = transcript_path(sid);
    std::ofstream out(path, std::ios::app);
    out << "not valid json\n";
    out.close();
    append_event(sid, UserEvent{"also_valid", 0ULL, {}});
    auto events = read_events(sid);
    assert(events.size() == 2);
    delete_session(sid);
}

static void test_index_update_delete() {
    const std::string sid = "test_index_001";
    SessionMeta meta;
    meta.session_id = sid;
    meta.created_at = 5000ULL;
    meta.model = "test-model";
    meta.cwd = "/tmp";
    update_index(sid, meta);
    {
        std::ifstream in(index_path());
        nlohmann::json idx;
        in >> idx;
        bool found = false;
        for (const auto& e : idx) {
            if (e.value("sid", "") == sid) { found = true; break; }
        }
        assert(found);
    }
    delete_session(sid);
    {
        std::ifstream in(index_path());
        nlohmann::json idx;
        in >> idx;
        bool found = false;
        for (const auto& e : idx) {
            if (e.value("sid", "") == sid) { found = true; break; }
        }
        assert(!found);
    }
}

int main() {
    test_user_event_roundtrip();
    test_assistant_event_roundtrip();
    test_tool_event_roundtrip();
    test_tool_event_error_roundtrip();
    test_system_event_roundtrip();
    test_turn_summary_roundtrip();
    test_approval_event_roundtrip();
    test_session_meta_roundtrip();
    test_variant_roundtrip();
    test_unknown_kind_fallback();
    test_transcript_append_read();
    test_transcript_empty_session();
    test_transcript_skips_malformed();
    test_index_update_delete();
    std::cout << "All persistence tests passed.\n";
    return 0;
}
