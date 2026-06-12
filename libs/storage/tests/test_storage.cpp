#include <merak/session_store.hpp>
#include <pqxx/pqxx>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

using namespace merak;

int main() {
    auto root = std::filesystem::temp_directory_path() / "merak-storage-test";
    std::filesystem::remove_all(root);

    const char* conn_str = std::getenv("MERAK_TEST_DB");
    if (!conn_str) {
        std::cerr << "SKIP: MERAK_TEST_DB not set (requires PostgreSQL)\n";
        return 0;
    }

    auto conn = std::make_shared<pqxx::connection>(conn_str);
    SessionStore store(conn);
    store.set_root(root);
    store.initialize();

    auto session = store.create_session("storage test");
    assert(store.get_session(session.id).has_value());

    auto run = store.create_run(session.id, "hello");
    assert(store.has_unfinished_run(session.id));
    auto sub_run = store.create_run(
        session.id, "sub task", run.id, "delegation_test", "researcher", "sub_run");
    auto loaded_sub_run = store.get_run(sub_run.id);
    assert(loaded_sub_run.has_value());
    assert(loaded_sub_run->parent_run_id == run.id);
    assert(loaded_sub_run->delegation_id == "delegation_test");
    assert(loaded_sub_run->agent_id == "researcher");
    assert(loaded_sub_run->run_kind == "sub_run");
    store.update_run_status(run.id, RunStatus::WaitingApproval);

    ApprovalRecord approval;
    approval.id = "approval_test";
    approval.run_id = run.id;
    approval.tool_name = "execute_bash";
    approval.arguments_json = R"({"command":"pwd"})";
    approval.tool_call_id = "call_test";
    store.create_approval(approval);
    assert(store.get_approval(approval.id)->status == ApprovalStatus::Pending);
    assert(store.resolve_approval(approval.id, ApprovalStatus::Allowed).status ==
           ApprovalStatus::Allowed);
    assert(store.resolve_approval(approval.id, ApprovalStatus::Allowed).status ==
           ApprovalStatus::Allowed);

    RuntimeEvent first{0, "", session.id, run.id, "run_started", {}};
    auto written = store.append_event(first);
    assert(written.seq == 1);
    assert(store.events_after(session.id, 0).size() == 1);

    std::ofstream corrupt(store.journal_path(session.id), std::ios::app);
    corrupt << R"({"incomplete":)";
    corrupt.close();
    assert(store.events_after(session.id, 0).size() == 1);

    store.update_run_status(run.id, RunStatus::Running);
    auto interrupted = store.interrupt_running_runs();
    assert(interrupted.size() == 1);
    assert(store.get_run(run.id)->status == RunStatus::Interrupted);
    std::filesystem::remove_all(root);
}
