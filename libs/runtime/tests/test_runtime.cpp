#include <merak/runtime_service.hpp>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <thread>

using namespace merak;

int main() {
    auto root = std::filesystem::temp_directory_path() / "merak-runtime-test";
    std::filesystem::remove_all(root);
    RuntimeService service(root);
    service.initialize();

    auto session = service.create_session("runtime test");
    auto events = service.events_after(session.id, 0);
    assert(events.size() == 1);
    assert(events.front().type == "session_created");

    auto run = service.create_run_record(session.id, "hello");
    assert(run.status == RunStatus::Queued);
    bool busy = false;
    try {
        service.create_run_record(session.id, "second");
    } catch (const RuntimeError& e) {
        busy = e.code() == "session_busy";
    }
    assert(busy);

    service.cancel_run(run.id);
    assert(service.get_run(run.id)->status == RunStatus::Cancelled);
    auto replay = service.events_after(session.id, 0);
    assert(replay.back().type == "run_cancelled");
    std::filesystem::remove_all(root);

    auto delegation_root = std::filesystem::temp_directory_path() / "merak-runtime-delegation-test";
    std::filesystem::remove_all(delegation_root);

    std::map<std::string, SubAgentConfig> agents;
    SubAgentConfig researcher;
    researcher.id = "researcher";
    researcher.system_prompt = "Research";
    agents[researcher.id] = researcher;
    SubAgentConfig reviewer;
    reviewer.id = "reviewer";
    reviewer.system_prompt = "Review";
    agents[reviewer.id] = reviewer;

    auto delegation_service = std::make_shared<RuntimeService>(
        delegation_root,
        RuntimeService::LoopFactory{},
        agents,
        [](const SubAgentConfig& agent, const std::string& task, RunControl& control) {
            AgentResponse response;
            response.text = agent.id + " handled " + task;
            response.total_input_tokens = 10;
            response.total_output_tokens = 5;
            response.has_usage = true;
            control.emit_text_delta(response.text);
            control.emit_usage(10, 5, true);
            return response;
        });
    delegation_service->initialize();
    auto delegation_session = delegation_service->create_session("delegation");
    DelegationRequest request;
    request.pattern = "fan_out";
    request.agent_ids = {"researcher", "reviewer"};
    request.task = "map runtime";
    request.aggregation = "all_results";

    auto delegation = delegation_service->start_delegation(delegation_session.id, request);
    assert(!delegation.delegation_id.empty());
    assert(!delegation.parent_run_id.empty());
    assert(delegation.session_id == delegation_session.id);

    bool completed = false;
    bool parent_completed = false;
    for (int i = 0; i < 50 && (!completed || !parent_completed); ++i) {
        for (const auto& event : delegation_service->events_after(delegation_session.id, 0)) {
            completed = completed || event.type == "delegation_completed";
            parent_completed = parent_completed ||
                (event.type == "run_completed" && event.run_id == delegation.parent_run_id);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    assert(completed);
    assert(parent_completed);

    auto delegation_events = delegation_service->events_after(delegation_session.id, 0);
    int sub_started = 0;
    int sub_completed = 0;
    bool saw_started = false;
    bool saw_parent_completion = false;
    for (const auto& event : delegation_events) {
        if (event.type == "delegation_started") {
            saw_started = true;
            assert(event.payload["delegation_id"] == delegation.delegation_id);
            assert(event.payload["pattern"] == "fan_out");
        }
        if (event.type == "sub_run_started") {
            ++sub_started;
            assert(event.payload["parent_run_id"] == delegation.parent_run_id);
            assert(event.payload["delegation_id"] == delegation.delegation_id);
            auto sub_run = delegation_service->get_run(event.payload["run_id"].get<std::string>());
            assert(sub_run.has_value());
            assert(sub_run->parent_run_id == delegation.parent_run_id);
            assert(sub_run->delegation_id == delegation.delegation_id);
            assert(!sub_run->agent_id.empty());
        }
        if (event.type == "sub_run_completed") ++sub_completed;
        if (event.type == "run_completed" && event.run_id == delegation.parent_run_id) {
            saw_parent_completion = true;
        }
    }
    assert(saw_started);
    assert(sub_started == 2);
    assert(sub_completed == 2);
    assert(saw_parent_completion);

    bool invalid_agent = false;
    try {
        DelegationRequest bad;
        bad.pattern = "fan_out";
        bad.agent_ids = {"missing"};
        bad.task = "x";
        delegation_service->start_delegation(delegation_session.id, bad);
    } catch (const RuntimeError& error) {
        invalid_agent = error.code() == "agent_not_found";
    }
    assert(invalid_agent);

    std::filesystem::remove_all(delegation_root);
}
