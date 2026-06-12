#pragma once
#include <merak/runtime_event.hpp>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <memory>
#include <pqxx/pqxx>

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
    std::string world_id;
    std::string agent_id;
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
    std::string parent_run_id;
    std::string delegation_id;
    std::string agent_id;
    std::string run_kind = "user";
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
    explicit SessionStore(std::shared_ptr<pqxx::connection> conn);
    ~SessionStore();
    void initialize();

    SessionRecord create_session(const std::string& title = "",
                                  const std::string& world_id = "",
                                  const std::string& agent_id = "");
    void update_session(const std::string& id, const std::string& title);
    SessionRecord archive_session(const std::string& id, bool archived);
    std::optional<SessionRecord> get_session(const std::string& id) const;
    std::vector<SessionRecord> list_sessions(const std::string& world_id = "") const;

    RunRecord create_run(
        const std::string& session_id,
        const std::string& message,
        const std::string& parent_run_id = "",
        const std::string& delegation_id = "",
        const std::string& agent_id = "",
        const std::string& run_kind = "user");
    std::optional<RunRecord> get_run(const std::string& id) const;
    bool has_unfinished_run(const std::string& session_id) const;
    void update_run_status(const std::string& id, RunStatus status, const std::string& error = "");

    ApprovalRecord create_approval(ApprovalRecord approval);
    std::optional<ApprovalRecord> get_approval(const std::string& id) const;
    ApprovalRecord resolve_approval(const std::string& id, ApprovalStatus status);

    RuntimeEvent append_event(RuntimeEvent event);
    std::vector<RuntimeEvent> events_after(const std::string& session_id, long long after) const;
    std::vector<RunRecord> interrupt_running_runs();

    void set_root(std::filesystem::path root);
    std::filesystem::path journal_path(const std::string& session_id) const;
    void set_plan(const std::string& plan_text);
    std::optional<std::string> get_plan() const;

    void save_checkpoint(const std::string& id, const std::string& run_id,
                         int turn_index, const std::string& turn_state,
                         int64_t input_tokens, int64_t output_tokens,
                         const std::string& pending_calls_json,
                         const std::string& compacted_summary,
                         const std::string& pipeline_snapshot_json);
    std::optional<std::string> load_latest_checkpoint_json(const std::string& run_id);
    std::vector<std::string> list_checkpoints_json(const std::string& run_id);
    void prune_checkpoints(const std::string& run_id, int keep_latest_n);

private:
    std::shared_ptr<pqxx::connection> conn_;
    std::filesystem::path root_;
    mutable std::mutex mutex_;
    mutable std::mutex plan_mutex_;
    std::string plan_text_;
    void exec(const std::string& sql);
};

} // namespace merak
