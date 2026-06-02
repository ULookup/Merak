#include <merak/runtime_service.hpp>
#include <cassert>
#include <filesystem>

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
}
