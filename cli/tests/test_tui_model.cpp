#include "tui/chat_model.hpp"
#include "tui/composer.hpp"
#include "tui/history_cell.hpp"
#include "tui/status_indicator.hpp"
#include <cassert>
#include <chrono>
#include <string>

using namespace merak;
using namespace merak::tui;
using namespace std::chrono_literals;

static void test_history_cells() {
    UserCell user("inspect config");
    assert(user.lines().at(0) == "> inspect config");

    AssistantCell assistant;
    assistant.append("hello\nworld");
    assert(assistant.lines().size() == 2);
    assert(assistant.lines().at(0) == "  hello");
    assert(assistant.lines().at(1) == "  world");

    ToolCall call{"call-1", "grep", R"({"pattern":"needle","path":"src"})"};
    ToolCell tool(call);
    tool.finish(ToolResult{"call-1", "one\nline\ntwo", false}, 120ms);
    auto rendered = tool.lines();
    assert(rendered.at(0) == "o Ran grep (120ms)");
    assert(rendered.at(1) == "  | needle in src");
    assert(rendered.at(2) == "  | one line two");

    ApprovalCell approval(call);
    assert(!approval.persisted());
    assert(approval.lines().at(0).find("[y/n]") != std::string::npos);
}

static void test_status_indicator() {
    StatusIndicator status;
    auto start = StatusIndicator::Clock::now();
    status.begin_turn(start);
    status.set_activity("Running grep");

    auto line = status.line_at(start + 7s);
    assert(line.find("Thinking") != std::string::npos);
    assert(line.find("7s") != std::string::npos);
    assert(line.find("Running grep") != std::string::npos);
    assert(line.find("thought for 7s") != std::string::npos);

    status.bump_stream_chars(20);
    line = status.line_at(start + 8s);
    assert(line.find("thought for") == std::string::npos);
    assert(line.find("~5 tokens") != std::string::npos);
}

static void test_composer_queue() {
    Composer composer;
    composer.set_text("first");
    assert(composer.submit(true) == SubmitKind::Queued);
    composer.set_text("second");
    assert(composer.submit(true) == SubmitKind::Queued);
    assert(composer.queued_count() == 2);
    assert(composer.edit_last_queued());
    assert(composer.text() == "second");
    assert(composer.queued_count() == 1);
    assert(composer.refill_next_queued());
    assert(composer.text() == "first");
}

static void test_chat_model() {
    ChatModel model;
    model.commit(std::make_unique<UserCell>("hi"));
    assert(model.drain_committed().size() == 1);
    assert(model.drain_committed().empty());

    model.begin_turn();
    model.on_agent_started("researcher");
    model.on_agent_step("researcher");
    auto rows = model.agent_rows();
    assert(rows.size() == 1);
    assert(rows.at(0).find("researcher") != std::string::npos);
    assert(rows.at(0).find("1 step") != std::string::npos);
}

int main() {
    test_history_cells();
    test_status_indicator();
    test_composer_queue();
    test_chat_model();
    return 0;
}
