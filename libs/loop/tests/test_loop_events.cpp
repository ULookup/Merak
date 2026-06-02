#include <merak/agent_loop.hpp>
#include <merak/sub_agent_runner.hpp>
#include <cassert>
#include <memory>
#include <vector>

using namespace merak;

static void test_cancel_flag() {
    AgentLoop loop({}, nullptr, nullptr, nullptr, nullptr, nullptr);
    assert(!loop.cancel_requested());
    loop.request_cancel();
    assert(loop.cancel_requested());
    loop.reset_cancel();
    assert(!loop.cancel_requested());
}

static void test_missing_sub_agent_emits_failure() {
    SubAgentRunner runner(nullptr, nullptr, nullptr);
    std::vector<SubAgentEvent> events;
    runner.set_observer([&](const SubAgentEvent& event) { events.push_back(event); });
    auto response = runner.delegate("missing", "task").get();
    assert(response.text == "Agent not found: missing");
    assert(events.size() == 2);
    assert(events.at(0).kind == SubAgentEventKind::Started);
    assert(events.at(1).kind == SubAgentEventKind::Failed);
}

int main() {
    test_cancel_flag();
    test_missing_sub_agent_emits_failure();
    return 0;
}
