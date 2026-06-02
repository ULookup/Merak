#pragma once
#include <merak/runtime_event.hpp>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace merak {

enum class RunStatus { Queued, Running, WaitingApproval, Completed, Failed, Cancelled, Interrupted };
enum class ApprovalStatus { Pending, Allowed, Denied };

std::string to_string(RunStatus status);
std::string to_string(ApprovalStatus status);
RunStatus run_status_from_string(const std::string& value);
ApprovalStatus approval_status_from_string(const std::string& value);

struct SessionRecord {
    std::string id;
    std::string title;
    long long last_seq = 0;
    std::string created_at;
    std::string updated_at;
    std::string archived_at;
};

struct RunRecord {
    std::string id;
    std::string session_id;
    RunStatus status = RunStatus::Queued;
    std::string user_message;
    std::string started_at;
    std::string finished_at;
    std::string error;
};

struct ApprovalRecord {
    std::string id;
    std::string run_id;
    std::string tool_name;
    std::string arguments_json;
    std::string tool_call_id;
    ApprovalStatus status = ApprovalStatus::Pending;
    std::string created_at;
    std::string resolved_at;
};

class SessionStore {
public:
    explicit SessionStore(std::filesystem::path root);
    ~SessionStore();
    void initialize();

    SessionRecord create_session(const std::string& title = "");
    std::optional<SessionRecord> get_session(const std::string& id) const;
    std::vector<SessionRecord> list_sessions() const;
    RunRecord create_run(const std::string& session_id, const std::string& message);
    std::optional<RunRecord> get_run(const std::string& id) const;
    bool has_unfinished_run(const std::string& session_id) const;
    void update_run_status(const std::string& id, RunStatus status, const std::string& error = "");
    ApprovalRecord create_approval(ApprovalRecord approval);
    std::optional<ApprovalRecord> get_approval(const std::string& id) const;
    ApprovalRecord resolve_approval(const std::string& id, ApprovalStatus status);
    RuntimeEvent append_event(RuntimeEvent event);
    std::vector<RuntimeEvent> events_after(const std::string& session_id, long long after) const;
    std::vector<RunRecord> interrupt_running_runs();
    std::filesystem::path journal_path(const std::string& session_id) const;

private:
    std::filesystem::path root_;
    sqlite3* db_ = nullptr;
    mutable std::mutex mutex_;
    void exec(const std::string& sql) const;
};

} // namespace merak
