#include <merak/session_store.hpp>
#include <cassert>
#include <filesystem>
#include <fstream>

using namespace merak;

int main() {
    auto root = std::filesystem::temp_directory_path() / "merak-storage-test";
    std::filesystem::remove_all(root);
    SessionStore store(root);
    store.initialize();

    auto session = store.create_session("storage test");
    assert(store.get_session(session.id).has_value());

    auto run = store.create_run(session.id, "hello");
    assert(store.has_unfinished_run(session.id));
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
