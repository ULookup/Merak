#pragma once
#include <merak/session_store.hpp>
#include <memory>
#include <mutex>
#include <pqxx/pqxx>

namespace merak {

class SessionStorePG {
public:
    explicit SessionStorePG(std::shared_ptr<pqxx::connection> conn);
    ~SessionStorePG();
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
    mutable std::mutex mutex_;
    void exec(const std::string& sql);
};

} // namespace merak
