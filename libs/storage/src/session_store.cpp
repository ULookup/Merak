#include <merak/session_store.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace merak {
namespace {

std::string now_iso() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream out;
    out << std::put_time(std::gmtime(&t), "%FT%TZ");
    return out.str();
}

std::string make_id(const char* prefix) {
    static std::atomic<unsigned long long> n = 0;
    auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::string(prefix) + "_" + std::to_string(ticks) + "_" + std::to_string(++n);
}

bool unfinished(const std::string& status) {
    return status == "queued" || status == "running" || status == "waiting_approval";
}

std::string null_or_empty(const pqxx::field& f) {
    return f.is_null() ? "" : f.as<std::string>();
}

SessionRecord session_from_row(const pqxx::row& row) {
    SessionRecord r;
    r.id = row["id"].as<std::string>();
    r.title = row["title"].as<std::string>();
    r.world_id = row["world_id"].as<std::string>();
    r.agent_id = row["agent_id"].as<std::string>();
    r.last_seq = row["last_seq"].as<long long>();
    r.created_at = row["created_at"].as<std::string>();
    r.updated_at = row["updated_at"].as<std::string>();
    r.archived_at = null_or_empty(row["archived_at"]);
    return r;
}

RunRecord run_from_row(const pqxx::row& row) {
    RunRecord r;
    r.id = row["id"].as<std::string>();
    r.session_id = row["session_id"].as<std::string>();
    r.status = run_status_from_string(row["status"].as<std::string>());
    r.user_message = row["user_message"].as<std::string>();
    r.started_at = row["started_at"].as<std::string>();
    r.finished_at = null_or_empty(row["finished_at"]);
    r.error = row["error"].as<std::string>();
    r.parent_run_id = row["parent_run_id"].as<std::string>();
    r.delegation_id = row["delegation_id"].as<std::string>();
    r.agent_id = row["agent_id"].as<std::string>();
    r.run_kind = row["run_kind"].as<std::string>();
    return r;
}

ApprovalRecord approval_from_row(const pqxx::row& row) {
    ApprovalRecord a;
    a.id = row["id"].as<std::string>();
    a.run_id = row["run_id"].as<std::string>();
    a.tool_name = row["tool_name"].as<std::string>();
    a.arguments_json = row["arguments_json"].as<std::string>();
    a.tool_call_id = row["tool_call_id"].as<std::string>();
    a.status = approval_status_from_string(row["status"].as<std::string>());
    a.created_at = row["created_at"].as<std::string>();
    a.resolved_at = null_or_empty(row["resolved_at"]);
    return a;
}

} // anonymous namespace

std::string to_string(RunStatus s) {
    switch (s) {
        case RunStatus::Queued: return "queued";
        case RunStatus::Running: return "running";
        case RunStatus::WaitingApproval: return "waiting_approval";
        case RunStatus::Completed: return "completed";
        case RunStatus::Failed: return "failed";
        case RunStatus::Cancelled: return "cancelled";
        case RunStatus::Interrupted: return "interrupted";
    }
    return "failed";
}
std::string to_string(ApprovalStatus s) {
    switch (s) {
        case ApprovalStatus::Pending: return "pending";
        case ApprovalStatus::Allowed: return "allowed";
        case ApprovalStatus::Denied: return "denied";
    }
    return "denied";
}
RunStatus run_status_from_string(const std::string& v) {
    if (v == "queued") return RunStatus::Queued;
    if (v == "running") return RunStatus::Running;
    if (v == "waiting_approval") return RunStatus::WaitingApproval;
    if (v == "completed") return RunStatus::Completed;
    if (v == "cancelled") return RunStatus::Cancelled;
    if (v == "interrupted") return RunStatus::Interrupted;
    return RunStatus::Failed;
}
ApprovalStatus approval_status_from_string(const std::string& v) {
    if (v == "pending") return ApprovalStatus::Pending;
    if (v == "allowed") return ApprovalStatus::Allowed;
    return ApprovalStatus::Denied;
}

SessionStore::SessionStore(std::shared_ptr<pqxx::connection> conn)
    : conn_(std::move(conn)) {}

SessionStore::~SessionStore() = default;

void SessionStore::exec(const std::string& sql) {
    pqxx::work txn(*conn_);
    txn.exec(sql);
    txn.commit();
}

void SessionStore::initialize() {
    std::lock_guard lock(mutex_);

    exec(R"(
        CREATE TABLE IF NOT EXISTS sessions (
            id TEXT PRIMARY KEY,
            title TEXT NOT NULL DEFAULT '',
            world_id TEXT NOT NULL DEFAULT '',
            agent_id TEXT NOT NULL DEFAULT '',
            last_seq BIGINT NOT NULL DEFAULT 0,
            created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
            updated_at TIMESTAMPTZ NOT NULL DEFAULT now(),
            archived_at TIMESTAMPTZ
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS runs (
            id TEXT PRIMARY KEY,
            session_id TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
            status TEXT NOT NULL DEFAULT 'queued',
            user_message TEXT NOT NULL DEFAULT '',
            started_at TIMESTAMPTZ NOT NULL DEFAULT now(),
            finished_at TIMESTAMPTZ,
            error TEXT NOT NULL DEFAULT '',
            parent_run_id TEXT NOT NULL DEFAULT '',
            delegation_id TEXT NOT NULL DEFAULT '',
            agent_id TEXT NOT NULL DEFAULT '',
            run_kind TEXT NOT NULL DEFAULT 'user'
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS approvals (
            id TEXT PRIMARY KEY,
            run_id TEXT NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
            tool_name TEXT NOT NULL DEFAULT '',
            arguments_json JSONB NOT NULL DEFAULT '{}',
            tool_call_id TEXT NOT NULL DEFAULT '',
            status TEXT NOT NULL DEFAULT 'pending',
            created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
            resolved_at TIMESTAMPTZ
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS run_checkpoints (
            id TEXT PRIMARY KEY,
            run_id TEXT NOT NULL REFERENCES runs(id) ON DELETE CASCADE,
            turn_index INTEGER NOT NULL DEFAULT 0,
            turn_state JSONB NOT NULL DEFAULT '{}',
            input_tokens_used BIGINT DEFAULT 0,
            output_tokens_used BIGINT DEFAULT 0,
            pending_calls_json JSONB DEFAULT '[]',
            compacted_summary TEXT DEFAULT '',
            pipeline_snapshot_json JSONB DEFAULT '{}',
            created_at TIMESTAMPTZ NOT NULL DEFAULT now()
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS runtime_events (
            id TEXT PRIMARY KEY,
            session_id TEXT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
            seq BIGINT NOT NULL,
            run_id TEXT NOT NULL DEFAULT '',
            type TEXT NOT NULL DEFAULT '',
            payload JSONB NOT NULL DEFAULT '{}',
            created_at TIMESTAMPTZ NOT NULL DEFAULT now()
        )
    )");

    exec("CREATE INDEX IF NOT EXISTS idx_sessions_world ON sessions(world_id)");
    exec("CREATE INDEX IF NOT EXISTS idx_sessions_agent ON sessions(agent_id)");
    exec("CREATE INDEX IF NOT EXISTS idx_runs_session ON runs(session_id)");
    exec("CREATE INDEX IF NOT EXISTS idx_approvals_run ON approvals(run_id)");
    exec("CREATE INDEX IF NOT EXISTS idx_checkpoints_run ON run_checkpoints(run_id, turn_index DESC)");
    exec("CREATE INDEX IF NOT EXISTS idx_events_session_seq ON runtime_events(session_id, seq)");
}

// --- Session CRUD ---

SessionRecord SessionStore::create_session(const std::string& title,
                                              const std::string& world_id,
                                              const std::string& agent_id) {
    std::lock_guard lock(mutex_);
    auto id = make_id("session");
    auto ts = now_iso();

    pqxx::work txn(*conn_);
    txn.exec_params(
        "INSERT INTO sessions (id, title, world_id, agent_id, last_seq, created_at, updated_at) "
        "VALUES ($1, $2, $3, $4, 0, $5, $5)",
        id, title, world_id, agent_id, ts);
    txn.commit();

    return *get_session(id);
}

void SessionStore::update_session(const std::string& id, const std::string& title) {
    std::lock_guard lock(mutex_);
    auto ts = now_iso();

    pqxx::work txn(*conn_);
    auto r = txn.exec_params(
        "UPDATE sessions SET title = $1, updated_at = $2 WHERE id = $3",
        title, ts, id);
    txn.commit();

    if (r.affected_rows() == 0) {
        throw std::runtime_error("session not found");
    }
}

SessionRecord SessionStore::archive_session(const std::string& id, bool archived) {
    std::lock_guard lock(mutex_);
    auto ts = now_iso();

    pqxx::work txn(*conn_);
    if (archived) {
        txn.exec_params(
            "UPDATE sessions SET archived_at = $1, updated_at = $2 WHERE id = $3",
            ts, ts, id);
    } else {
        txn.exec_params(
            "UPDATE sessions SET archived_at = NULL, updated_at = $1 WHERE id = $2",
            ts, id);
    }
    txn.commit();

    auto session = get_session(id);
    if (!session) {
        throw std::runtime_error("session not found");
    }
    return *session;
}

std::optional<SessionRecord> SessionStore::get_session(const std::string& id) const {
    std::lock_guard lock(mutex_);

    pqxx::work txn(*conn_);
    auto r = txn.exec_params(
        "SELECT id, title, world_id, agent_id, last_seq, created_at, updated_at, archived_at "
        "FROM sessions WHERE id = $1",
        id);
    txn.commit();

    if (r.empty()) return std::nullopt;
    return session_from_row(r[0]);
}

std::vector<SessionRecord> SessionStore::list_sessions(const std::string& world_id) const {
    std::lock_guard lock(mutex_);

    pqxx::work txn(*conn_);
    pqxx::result r;
    if (world_id.empty()) {
        r = txn.exec_params(
            "SELECT id, title, world_id, agent_id, last_seq, created_at, updated_at, archived_at "
            "FROM sessions ORDER BY updated_at DESC");
    } else {
        r = txn.exec_params(
            "SELECT id, title, world_id, agent_id, last_seq, created_at, updated_at, archived_at "
            "FROM sessions WHERE world_id = $1 ORDER BY updated_at DESC",
            world_id);
    }
    txn.commit();

    std::vector<SessionRecord> out;
    for (const auto& row : r) {
        out.push_back(session_from_row(row));
    }
    return out;
}

// --- Run CRUD ---

RunRecord SessionStore::create_run(
    const std::string& session_id,
    const std::string& message,
    const std::string& parent_run_id,
    const std::string& delegation_id,
    const std::string& agent_id,
    const std::string& run_kind) {

    std::lock_guard lock(mutex_);
    auto id = make_id("run");
    auto ts = now_iso();

    pqxx::work txn(*conn_);
    txn.exec_params(
        "INSERT INTO runs (id, session_id, status, user_message, started_at, finished_at, error, "
        "parent_run_id, delegation_id, agent_id, run_kind) "
        "VALUES ($1, $2, $3, $4, $5, NULL, '', $6, $7, $8, $9)",
        id, session_id, to_string(RunStatus::Queued), message, ts,
        parent_run_id, delegation_id, agent_id, run_kind);
    txn.commit();

    return *get_run(id);
}

std::optional<RunRecord> SessionStore::get_run(const std::string& id) const {
    std::lock_guard lock(mutex_);

    pqxx::work txn(*conn_);
    auto r = txn.exec_params(
        "SELECT id, session_id, status, user_message, started_at, finished_at, error, "
        "parent_run_id, delegation_id, agent_id, run_kind "
        "FROM runs WHERE id = $1",
        id);
    txn.commit();

    if (r.empty()) return std::nullopt;
    return run_from_row(r[0]);
}

bool SessionStore::has_unfinished_run(const std::string& session_id) const {
    std::lock_guard lock(mutex_);

    pqxx::work txn(*conn_);
    auto r = txn.exec_params(
        "SELECT status FROM runs WHERE session_id = $1 AND parent_run_id = ''",
        session_id);

    for (const auto& row : r) {
        if (unfinished(row["status"].as<std::string>())) {
            txn.commit();
            return true;
        }
    }
    txn.commit();
    return false;
}

void SessionStore::update_run_status(const std::string& id, RunStatus status,
                                        const std::string& error) {
    std::lock_guard lock(mutex_);
    auto status_str = to_string(status);

    std::optional<std::string> finished_at_val;
    if (status != RunStatus::Running && status != RunStatus::WaitingApproval) {
        finished_at_val = now_iso();
    }

    pqxx::work txn(*conn_);
    auto r = txn.exec_params(
        "UPDATE runs SET status = $1, finished_at = $2, error = $3 WHERE id = $4",
        status_str, finished_at_val, error, id);
    txn.commit();

    if (r.affected_rows() == 0) {
        throw std::runtime_error("run not found: " + id);
    }
}

// --- Approval CRUD ---

ApprovalRecord SessionStore::create_approval(ApprovalRecord approval) {
    std::lock_guard lock(mutex_);

    if (approval.id.empty()) {
        approval.id = make_id("approval");
    }
    approval.created_at = now_iso();

    pqxx::work txn(*conn_);
    txn.exec_params(
        "INSERT INTO approvals (id, run_id, tool_name, arguments_json, tool_call_id, "
        "status, created_at) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7)",
        approval.id, approval.run_id, approval.tool_name, approval.arguments_json,
        approval.tool_call_id, to_string(approval.status), approval.created_at);
    txn.commit();

    return approval;
}

std::optional<ApprovalRecord> SessionStore::get_approval(const std::string& id) const {
    std::lock_guard lock(mutex_);

    pqxx::work txn(*conn_);
    auto r = txn.exec_params(
        "SELECT id, run_id, tool_name, arguments_json, tool_call_id, status, created_at, resolved_at "
        "FROM approvals WHERE id = $1",
        id);
    txn.commit();

    if (r.empty()) return std::nullopt;
    return approval_from_row(r[0]);
}

ApprovalRecord SessionStore::resolve_approval(const std::string& id, ApprovalStatus status) {
    auto existing = get_approval(id);
    if (!existing) {
        throw std::runtime_error("approval not found");
    }
    if (existing->status != ApprovalStatus::Pending) {
        return *existing;
    }

    {
        std::lock_guard lock(mutex_);
        auto ts = now_iso();
        pqxx::work txn(*conn_);
        txn.exec_params(
            "UPDATE approvals SET status = $1, resolved_at = $2 WHERE id = $3",
            to_string(status), ts, id);
        txn.commit();
    }

    return *get_approval(id);
}

// --- Events ---

RuntimeEvent SessionStore::append_event(RuntimeEvent event) {
    std::lock_guard lock(mutex_);

    pqxx::work txn(*conn_);
    auto r = txn.exec_params(
        "SELECT last_seq FROM sessions WHERE id = $1 FOR UPDATE",
        event.session_id);
    if (r.empty()) {
        throw std::runtime_error("session not found");
    }

    event.seq = r[0]["last_seq"].as<long long>() + 1;
    event.timestamp = now_iso();

    txn.exec_params(
        "UPDATE sessions SET last_seq = $1, updated_at = $2 WHERE id = $3",
        event.seq, event.timestamp, event.session_id);

    txn.exec_params(
        "INSERT INTO runtime_events (id, session_id, seq, run_id, type, payload, created_at) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7)",
        make_id("evt"), event.session_id, event.seq, event.run_id, event.type,
        event.payload.dump(), event.timestamp);

    txn.commit();
    return event;
}

std::vector<RuntimeEvent> SessionStore::events_after(const std::string& session_id,
                                                       long long after) const {
    std::lock_guard lock(mutex_);

    pqxx::work txn(*conn_);
    auto r = txn.exec_params(
        "SELECT seq, created_at, session_id, run_id, type, payload FROM runtime_events "
        "WHERE session_id = $1 AND seq > $2 ORDER BY seq ASC",
        session_id, after);
    txn.commit();

    std::vector<RuntimeEvent> out;
    for (const auto& row : r) {
        RuntimeEvent e;
        e.seq = row["seq"].as<long long>();
        e.timestamp = row["created_at"].as<std::string>();
        e.session_id = row["session_id"].as<std::string>();
        e.run_id = row["run_id"].as<std::string>();
        e.type = row["type"].as<std::string>();
        try {
            e.payload = nlohmann::json::parse(row["payload"].as<std::string>());
        } catch (...) {
            e.payload = nlohmann::json::object();
        }
        out.push_back(std::move(e));
    }
    return out;
}

// --- Interrupt running runs ---

std::vector<RunRecord> SessionStore::interrupt_running_runs() {
    std::lock_guard lock(mutex_);

    pqxx::work txn(*conn_);
    auto ts = now_iso();

    // Update first so returned rows reflect the new status.
    txn.exec_params(
        "UPDATE runs SET status = 'interrupted', finished_at = $1 WHERE status = 'running'",
        ts);

    // Re-query to get the updated rows.
    auto r = txn.exec_params(
        "SELECT id, session_id, status, user_message, started_at, finished_at, error, "
        "parent_run_id, delegation_id, agent_id, run_kind "
        "FROM runs WHERE status = 'interrupted' AND finished_at = $1",
        ts);

    txn.commit();

    std::vector<RunRecord> out;
    for (const auto& row : r) {
        out.push_back(run_from_row(row));
    }
    return out;
}

// --- Checkpoint operations ---

void SessionStore::save_checkpoint(const std::string& id, const std::string& run_id,
                                     int turn_index, const std::string& turn_state,
                                     int64_t input_tokens, int64_t output_tokens,
                                     const std::string& pending_calls_json,
                                     const std::string& compacted_summary,
                                     const std::string& pipeline_snapshot_json) {
    std::lock_guard lock(mutex_);

    pqxx::work txn(*conn_);
    txn.exec_params(
        "INSERT INTO run_checkpoints (id, run_id, turn_index, turn_state, "
        "input_tokens_used, output_tokens_used, pending_calls_json, "
        "compacted_summary, pipeline_snapshot_json) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9) "
        "ON CONFLICT (id) DO UPDATE SET "
        "run_id = EXCLUDED.run_id, "
        "turn_index = EXCLUDED.turn_index, "
        "turn_state = EXCLUDED.turn_state, "
        "input_tokens_used = EXCLUDED.input_tokens_used, "
        "output_tokens_used = EXCLUDED.output_tokens_used, "
        "pending_calls_json = EXCLUDED.pending_calls_json, "
        "compacted_summary = EXCLUDED.compacted_summary, "
        "pipeline_snapshot_json = EXCLUDED.pipeline_snapshot_json, "
        "created_at = now()",
        id, run_id, turn_index, turn_state,
        input_tokens, output_tokens, pending_calls_json,
        compacted_summary, pipeline_snapshot_json);
    txn.commit();
}

std::optional<std::string> SessionStore::load_latest_checkpoint_json(const std::string& run_id) {
    std::lock_guard lock(mutex_);

    pqxx::work txn(*conn_);
    auto r = txn.exec_params(
        "SELECT id, run_id, turn_index, turn_state, input_tokens_used, output_tokens_used, "
        "pending_calls_json, compacted_summary, pipeline_snapshot_json, created_at "
        "FROM run_checkpoints WHERE run_id = $1 ORDER BY turn_index DESC LIMIT 1",
        run_id);
    txn.commit();

    if (r.empty()) return std::nullopt;

    const auto& row = r[0];
    nlohmann::json j{
        {"id", row["id"].as<std::string>()},
        {"run_id", row["run_id"].as<std::string>()},
        {"turn_index", row["turn_index"].as<int>()},
        {"turn_state", row["turn_state"].as<std::string>()},
        {"input_tokens_used", row["input_tokens_used"].as<int64_t>()},
        {"output_tokens_used", row["output_tokens_used"].as<int64_t>()},
        {"pending_calls_json", row["pending_calls_json"].as<std::string>()},
        {"compacted_summary", row["compacted_summary"].as<std::string>()},
        {"pipeline_snapshot_json", row["pipeline_snapshot_json"].as<std::string>()},
        {"created_at", row["created_at"].as<std::string>()}
    };
    return j.dump();
}

std::vector<std::string> SessionStore::list_checkpoints_json(const std::string& run_id) {
    std::lock_guard lock(mutex_);

    pqxx::work txn(*conn_);
    auto r = txn.exec_params(
        "SELECT id, run_id, turn_index, turn_state, input_tokens_used, output_tokens_used, "
        "pending_calls_json, compacted_summary, pipeline_snapshot_json, created_at "
        "FROM run_checkpoints WHERE run_id = $1 ORDER BY turn_index ASC",
        run_id);
    txn.commit();

    std::vector<std::string> out;
    for (const auto& row : r) {
        nlohmann::json j{
            {"id", row["id"].as<std::string>()},
            {"run_id", row["run_id"].as<std::string>()},
            {"turn_index", row["turn_index"].as<int>()},
            {"turn_state", row["turn_state"].as<std::string>()},
            {"input_tokens_used", row["input_tokens_used"].as<int64_t>()},
            {"output_tokens_used", row["output_tokens_used"].as<int64_t>()},
            {"pending_calls_json", row["pending_calls_json"].as<std::string>()},
            {"compacted_summary", row["compacted_summary"].as<std::string>()},
            {"pipeline_snapshot_json", row["pipeline_snapshot_json"].as<std::string>()},
            {"created_at", row["created_at"].as<std::string>()}
        };
        out.push_back(j.dump());
    }
    return out;
}

void SessionStore::prune_checkpoints(const std::string& run_id, int keep_latest_n) {
    std::lock_guard lock(mutex_);

    pqxx::work txn(*conn_);
    if (keep_latest_n <= 0) {
        txn.exec_params("DELETE FROM run_checkpoints WHERE run_id = $1", run_id);
    } else {
        txn.exec_params(
            "DELETE FROM run_checkpoints WHERE run_id = $1 AND id NOT IN "
            "(SELECT id FROM run_checkpoints WHERE run_id = $1 "
            "ORDER BY turn_index DESC LIMIT $2)",
            run_id, keep_latest_n);
    }
    txn.commit();
}

void SessionStore::set_root(std::filesystem::path root) {
    root_ = std::move(root);
}

std::filesystem::path SessionStore::journal_path(const std::string& session_id) const {
    return root_ / "journal" / (session_id + ".jsonl");
}

void SessionStore::set_plan(const std::string& plan_text) {
    std::lock_guard lock(plan_mutex_);
    plan_text_ = plan_text;
}

std::optional<std::string> SessionStore::get_plan() const {
    std::lock_guard lock(plan_mutex_);
    if (plan_text_.empty()) return std::nullopt;
    return plan_text_;
}

} // namespace merak
