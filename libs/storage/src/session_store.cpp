#include <merak/session_store.hpp>
#include <nlohmann/json.hpp>
#include <sqlite3.h>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <fstream>
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
void bind_text(sqlite3_stmt* stmt, int index, const std::string& value) {
	sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT);
}
void expect_done(sqlite3_stmt* stmt, const char* operation) {
	if (sqlite3_step(stmt) != SQLITE_DONE) {
		throw std::runtime_error(std::string(operation) + " failed");
	}
}
std::string col(sqlite3_stmt* stmt, int index) {
	auto* text = sqlite3_column_text(stmt, index);
	return text ? reinterpret_cast<const char*>(text) : "";
}
bool has_column(sqlite3* db, const std::string& table, const std::string& column) {
	sqlite3_stmt* stmt = nullptr;
	auto sql = "PRAGMA table_info(" + table + ")";
	sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
	bool found = false;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		if (col(stmt, 1) == column) {
			found = true;
			break;
		}
	}
	sqlite3_finalize(stmt);
	return found;
}
void add_column_if_missing(sqlite3* db, const std::string& table,
                           const std::string& column, const std::string& definition) {
	if (has_column(db, table, column)) return;
	char* error = nullptr;
	auto sql = "ALTER TABLE " + table + " ADD COLUMN " + column + " " + definition;
	if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
		std::string message = error ? error : "sqlite error";
		sqlite3_free(error);
		throw std::runtime_error(message);
	}
}
}

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

SessionStore::SessionStore(std::filesystem::path root) : root_(std::move(root)) {}
SessionStore::~SessionStore() { if (db_) sqlite3_close(db_); }
void SessionStore::exec(const std::string& sql) const {
	char* error = nullptr;
	if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
		std::string message = error ? error : "sqlite error";
		sqlite3_free(error);
		throw std::runtime_error(message);
	}
}
void SessionStore::initialize() {
	std::filesystem::create_directories(root_ / "sessions");
	if (sqlite3_open((root_ / "runtime.sqlite3").string().c_str(), &db_) != SQLITE_OK)
		throw std::runtime_error("Cannot open runtime.sqlite3");
	exec("PRAGMA journal_mode=WAL;");
	exec("CREATE TABLE IF NOT EXISTS sessions(id TEXT PRIMARY KEY,title TEXT,world_id TEXT NOT NULL DEFAULT '',agent_id TEXT NOT NULL DEFAULT '',last_seq INTEGER NOT NULL DEFAULT 0,created_at TEXT,updated_at TEXT,archived_at TEXT);");
	add_column_if_missing(db_, "sessions", "world_id", "TEXT NOT NULL DEFAULT ''");
	add_column_if_missing(db_, "sessions", "agent_id", "TEXT NOT NULL DEFAULT ''");
	exec("CREATE TABLE IF NOT EXISTS runs(id TEXT PRIMARY KEY,session_id TEXT,status TEXT,user_message TEXT,started_at TEXT,finished_at TEXT,error TEXT,parent_run_id TEXT NOT NULL DEFAULT '',delegation_id TEXT NOT NULL DEFAULT '',agent_id TEXT NOT NULL DEFAULT '',run_kind TEXT NOT NULL DEFAULT 'user');");
	add_column_if_missing(db_, "runs", "parent_run_id", "TEXT NOT NULL DEFAULT ''");
	add_column_if_missing(db_, "runs", "delegation_id", "TEXT NOT NULL DEFAULT ''");
	add_column_if_missing(db_, "runs", "agent_id", "TEXT NOT NULL DEFAULT ''");
	add_column_if_missing(db_, "runs", "run_kind", "TEXT NOT NULL DEFAULT 'user'");
	exec("CREATE TABLE IF NOT EXISTS approvals(id TEXT PRIMARY KEY,run_id TEXT,tool_name TEXT,arguments_json TEXT,tool_call_id TEXT,status TEXT,created_at TEXT,resolved_at TEXT);");
	exec("CREATE TABLE IF NOT EXISTS run_checkpoints(id TEXT PRIMARY KEY,run_id TEXT NOT NULL,turn_index INTEGER NOT NULL DEFAULT 0,turn_state TEXT NOT NULL DEFAULT '{}',input_tokens_used INTEGER DEFAULT 0,output_tokens_used INTEGER DEFAULT 0,pending_calls_json TEXT DEFAULT '[]',compacted_summary TEXT DEFAULT '',pipeline_snapshot_json TEXT DEFAULT '{}',created_at TEXT NOT NULL DEFAULT (datetime('now')));");
	exec("CREATE INDEX IF NOT EXISTS idx_checkpoints_run ON run_checkpoints(run_id, turn_index DESC);");
	for (const auto& entry : std::filesystem::directory_iterator(root_ / "sessions")) {
		if (!entry.is_regular_file() || entry.path().extension() != ".jsonl") continue;
		auto session_id = entry.path().stem().string();
		long long last_seq = 0;
		std::ifstream input(entry.path());
		std::string line;
		while (std::getline(input, line)) {
			try {
				last_seq = std::max(last_seq,
					nlohmann::json::parse(line).value("seq", 0LL));
			} catch (...) {
				break;
			}
		}
		sqlite3_stmt* stmt = nullptr;
		sqlite3_prepare_v2(db_,
			"UPDATE sessions SET last_seq=MAX(last_seq,?) WHERE id=?",
			-1, &stmt, nullptr);
		sqlite3_bind_int64(stmt, 1, last_seq);
		bind_text(stmt, 2, session_id);
		sqlite3_step(stmt);
		sqlite3_finalize(stmt);
	}
}
SessionRecord SessionStore::create_session(const std::string& title, const std::string& world_id, const std::string& agent_id) {
	std::lock_guard lock(mutex_);
	SessionRecord r{make_id("session"), title, world_id, agent_id, 0, now_iso(), now_iso(), ""};
	sqlite3_stmt* s = nullptr;
	sqlite3_prepare_v2(db_, "INSERT INTO sessions(id,title,world_id,agent_id,last_seq,created_at,updated_at,archived_at) VALUES(?,?,?,?,?,?,?,?)", -1, &s, nullptr);
	bind_text(s,1,r.id); bind_text(s,2,r.title); bind_text(s,3,r.world_id); bind_text(s,4,r.agent_id);
	sqlite3_bind_int64(s,5,0);
	bind_text(s,6,r.created_at); bind_text(s,7,r.updated_at); bind_text(s,8,"");
	if (sqlite3_step(s) != SQLITE_DONE) { sqlite3_finalize(s); throw std::runtime_error("create session failed"); }
	sqlite3_finalize(s); return r;
}
void SessionStore::update_session(const std::string& id, const std::string& title) {
	std::lock_guard lock(mutex_);
	sqlite3_stmt* s = nullptr;
	sqlite3_prepare_v2(db_,
		"UPDATE sessions SET title = ?, updated_at = ? WHERE id = ?",
		-1, &s, nullptr);
	bind_text(s, 1, title);
	bind_text(s, 2, now_iso());
	bind_text(s, 3, id);
	if (sqlite3_step(s) != SQLITE_DONE) {
		sqlite3_finalize(s);
		throw std::runtime_error("update session failed");
	}
	sqlite3_finalize(s);
}
SessionRecord SessionStore::archive_session(const std::string& id, bool archived) {
	std::lock_guard lock(mutex_);
	sqlite3_stmt* s = nullptr;
	sqlite3_prepare_v2(db_,
		"UPDATE sessions SET archived_at = ?, updated_at = ? WHERE id = ?",
		-1, &s, nullptr);
	auto timestamp = now_iso();
	bind_text(s, 1, archived ? timestamp : "");
	bind_text(s, 2, timestamp);
	bind_text(s, 3, id);
	expect_done(s, "archive session");
	sqlite3_finalize(s);

	sqlite3_prepare_v2(db_,
		"SELECT id,title,world_id,agent_id,last_seq,created_at,updated_at,archived_at FROM sessions WHERE id=?",
		-1, &s, nullptr);
	bind_text(s, 1, id);
	if (sqlite3_step(s) != SQLITE_ROW) {
		sqlite3_finalize(s);
		throw std::runtime_error("session not found");
	}
	SessionRecord r{col(s,0),col(s,1),col(s,2),col(s,3),sqlite3_column_int64(s,4),col(s,5),col(s,6),col(s,7)};
	sqlite3_finalize(s);
	return r;
}
std::optional<SessionRecord> SessionStore::get_session(const std::string& id) const {
	std::lock_guard lock(mutex_); sqlite3_stmt* s=nullptr;
	sqlite3_prepare_v2(db_,"SELECT id,title,world_id,agent_id,last_seq,created_at,updated_at,archived_at FROM sessions WHERE id=?",-1,&s,nullptr); bind_text(s,1,id);
	if(sqlite3_step(s)!=SQLITE_ROW){sqlite3_finalize(s);return std::nullopt;}
	SessionRecord r{col(s,0),col(s,1),col(s,2),col(s,3),sqlite3_column_int64(s,4),col(s,5),col(s,6),col(s,7)}; sqlite3_finalize(s); return r;
}
std::vector<SessionRecord> SessionStore::list_sessions(const std::string& world_id) const {
	std::lock_guard lock(mutex_); std::vector<SessionRecord> out; sqlite3_stmt* s=nullptr;
	if (world_id.empty()) {
		sqlite3_prepare_v2(db_,"SELECT id,title,world_id,agent_id,last_seq,created_at,updated_at,archived_at FROM sessions ORDER BY updated_at DESC",-1,&s,nullptr);
	} else {
		sqlite3_prepare_v2(db_,"SELECT id,title,world_id,agent_id,last_seq,created_at,updated_at,archived_at FROM sessions WHERE world_id=? ORDER BY updated_at DESC",-1,&s,nullptr);
		bind_text(s,1,world_id);
	}
	while(sqlite3_step(s)==SQLITE_ROW) out.push_back({col(s,0),col(s,1),col(s,2),col(s,3),sqlite3_column_int64(s,4),col(s,5),col(s,6),col(s,7)});
	sqlite3_finalize(s); return out;
}
RunRecord SessionStore::create_run(const std::string& session_id,const std::string& message,const std::string& parent_run_id,const std::string& delegation_id,const std::string& agent_id,const std::string& run_kind) {
	std::lock_guard lock(mutex_); RunRecord r{make_id("run"),session_id,RunStatus::Queued,message,now_iso(),"","",parent_run_id,delegation_id,agent_id,run_kind}; sqlite3_stmt* s=nullptr;
	sqlite3_prepare_v2(db_,"INSERT INTO runs(id,session_id,status,user_message,started_at,finished_at,error,parent_run_id,delegation_id,agent_id,run_kind) VALUES(?,?,?,?,?,?,?,?,?,?,?)",-1,&s,nullptr); bind_text(s,1,r.id);bind_text(s,2,r.session_id);bind_text(s,3,to_string(r.status));bind_text(s,4,message);bind_text(s,5,r.started_at);bind_text(s,6,"");bind_text(s,7,"");bind_text(s,8,parent_run_id);bind_text(s,9,delegation_id);bind_text(s,10,agent_id);bind_text(s,11,run_kind);
	if(sqlite3_step(s)!=SQLITE_DONE){sqlite3_finalize(s);throw std::runtime_error("create run failed");} sqlite3_finalize(s); return r;
}
std::optional<RunRecord> SessionStore::get_run(const std::string& id) const {
	std::lock_guard lock(mutex_);sqlite3_stmt*s=nullptr;
	sqlite3_prepare_v2(db_,"SELECT id,session_id,status,user_message,started_at,finished_at,error,parent_run_id,delegation_id,agent_id,run_kind FROM runs WHERE id=?",-1,&s,nullptr);bind_text(s,1,id);
	if(sqlite3_step(s)!=SQLITE_ROW){sqlite3_finalize(s);return std::nullopt;} RunRecord r{col(s,0),col(s,1),run_status_from_string(col(s,2)),col(s,3),col(s,4),col(s,5),col(s,6),col(s,7),col(s,8),col(s,9),col(s,10)};sqlite3_finalize(s);return r;
}
bool SessionStore::has_unfinished_run(const std::string& session_id) const {
	std::lock_guard lock(mutex_);sqlite3_stmt*s=nullptr;sqlite3_prepare_v2(db_,"SELECT status FROM runs WHERE session_id=? AND parent_run_id=''",-1,&s,nullptr);bind_text(s,1,session_id);bool found=false;
	while(sqlite3_step(s)==SQLITE_ROW) if(unfinished(col(s,0))){found=true;break;} sqlite3_finalize(s);return found;
}
void SessionStore::update_run_status(const std::string& id,RunStatus status,const std::string& error) {
	std::lock_guard lock(mutex_);sqlite3_stmt*s=nullptr;sqlite3_prepare_v2(db_,"UPDATE runs SET status=?,finished_at=?,error=? WHERE id=?",-1,&s,nullptr);bind_text(s,1,to_string(status));bind_text(s,2,status==RunStatus::Running||status==RunStatus::WaitingApproval?"":now_iso());bind_text(s,3,error);bind_text(s,4,id);expect_done(s,"update run status");sqlite3_finalize(s);
}
ApprovalRecord SessionStore::create_approval(ApprovalRecord a) {
	std::lock_guard lock(mutex_);if(a.id.empty())a.id=make_id("approval");a.created_at=now_iso();sqlite3_stmt*s=nullptr;sqlite3_prepare_v2(db_,"INSERT INTO approvals VALUES(?,?,?,?,?,?,?,?)",-1,&s,nullptr);bind_text(s,1,a.id);bind_text(s,2,a.run_id);bind_text(s,3,a.tool_name);bind_text(s,4,a.arguments_json);bind_text(s,5,a.tool_call_id);bind_text(s,6,to_string(a.status));bind_text(s,7,a.created_at);bind_text(s,8,"");sqlite3_step(s);sqlite3_finalize(s);return a;
}
std::optional<ApprovalRecord> SessionStore::get_approval(const std::string&id)const{
	std::lock_guard lock(mutex_);sqlite3_stmt*s=nullptr;sqlite3_prepare_v2(db_,"SELECT id,run_id,tool_name,arguments_json,tool_call_id,status,created_at,resolved_at FROM approvals WHERE id=?",-1,&s,nullptr);bind_text(s,1,id);if(sqlite3_step(s)!=SQLITE_ROW){sqlite3_finalize(s);return std::nullopt;}ApprovalRecord a{col(s,0),col(s,1),col(s,2),col(s,3),col(s,4),approval_status_from_string(col(s,5)),col(s,6),col(s,7)};sqlite3_finalize(s);return a;
}
ApprovalRecord SessionStore::resolve_approval(const std::string&id,ApprovalStatus status){auto existing=get_approval(id);if(!existing)throw std::runtime_error("approval not found");if(existing->status!=ApprovalStatus::Pending)return *existing;{std::lock_guard lock(mutex_);sqlite3_stmt*s=nullptr;sqlite3_prepare_v2(db_,"UPDATE approvals SET status=?,resolved_at=? WHERE id=?",-1,&s,nullptr);bind_text(s,1,to_string(status));bind_text(s,2,now_iso());bind_text(s,3,id);sqlite3_step(s);sqlite3_finalize(s);}return *get_approval(id);}
std::filesystem::path SessionStore::journal_path(const std::string&id)const{return root_/"sessions"/(id+".jsonl");}
RuntimeEvent SessionStore::append_event(RuntimeEvent e){std::lock_guard lock(mutex_);sqlite3_stmt*s=nullptr;sqlite3_prepare_v2(db_,"SELECT last_seq FROM sessions WHERE id=?",-1,&s,nullptr);bind_text(s,1,e.session_id);if(sqlite3_step(s)!=SQLITE_ROW){sqlite3_finalize(s);throw std::runtime_error("session not found");}e.seq=sqlite3_column_int64(s,0)+1;sqlite3_finalize(s);e.timestamp=now_iso();std::ofstream out(journal_path(e.session_id),std::ios::app);out<<nlohmann::json(e).dump()<<'\n';out.flush();if(!out)throw std::runtime_error("journal append failed");sqlite3_prepare_v2(db_,"UPDATE sessions SET last_seq=?,updated_at=? WHERE id=?",-1,&s,nullptr);sqlite3_bind_int64(s,1,e.seq);bind_text(s,2,e.timestamp);bind_text(s,3,e.session_id);expect_done(s,"update journal index");sqlite3_finalize(s);return e;}
std::vector<RuntimeEvent>SessionStore::events_after(const std::string&id,long long after)const{std::lock_guard lock(mutex_);std::vector<RuntimeEvent>out;std::ifstream in(journal_path(id));std::string line;while(std::getline(in,line)){try{auto e=nlohmann::json::parse(line).get<RuntimeEvent>();if(e.seq>after)out.push_back(std::move(e));}catch(...){continue;}}return out;}
std::vector<RunRecord>SessionStore::interrupt_running_runs(){std::vector<RunRecord>out;std::lock_guard lock(mutex_);sqlite3_stmt*s=nullptr;sqlite3_prepare_v2(db_,"SELECT id,session_id,status,user_message,started_at,finished_at,error,parent_run_id,delegation_id,agent_id,run_kind FROM runs WHERE status='running'",-1,&s,nullptr);while(sqlite3_step(s)==SQLITE_ROW)out.push_back({col(s,0),col(s,1),RunStatus::Running,col(s,3),col(s,4),col(s,5),col(s,6),col(s,7),col(s,8),col(s,9),col(s,10)});sqlite3_finalize(s);for(auto&r:out){sqlite3_prepare_v2(db_,"UPDATE runs SET status='interrupted',finished_at=? WHERE id=?",-1,&s,nullptr);bind_text(s,1,now_iso());bind_text(s,2,r.id);sqlite3_step(s);sqlite3_finalize(s);}return out;}

// --- Checkpoint operations ---

void SessionStore::save_checkpoint(const std::string& id, const std::string& run_id,
                                   int turn_index, const std::string& turn_state,
                                   int64_t input_tokens, int64_t output_tokens,
                                   const std::string& pending_calls_json,
                                   const std::string& compacted_summary,
                                   const std::string& pipeline_snapshot_json) {
	std::lock_guard lock(mutex_);
	sqlite3_stmt* s = nullptr;
	sqlite3_prepare_v2(db_,
		"INSERT OR REPLACE INTO run_checkpoints(id,run_id,turn_index,turn_state,input_tokens_used,output_tokens_used,pending_calls_json,compacted_summary,pipeline_snapshot_json)"
		" VALUES(?,?,?,?,?,?,?,?,?)",
		-1, &s, nullptr);
	bind_text(s, 1, id);
	bind_text(s, 2, run_id);
	sqlite3_bind_int(s, 3, turn_index);
	bind_text(s, 4, turn_state);
	sqlite3_bind_int64(s, 5, input_tokens);
	sqlite3_bind_int64(s, 6, output_tokens);
	bind_text(s, 7, pending_calls_json);
	bind_text(s, 8, compacted_summary);
	bind_text(s, 9, pipeline_snapshot_json);
	expect_done(s, "save checkpoint");
	sqlite3_finalize(s);
}

std::optional<std::string> SessionStore::load_latest_checkpoint_json(const std::string& run_id) {
	std::lock_guard lock(mutex_);
	sqlite3_stmt* s = nullptr;
	sqlite3_prepare_v2(db_,
		"SELECT id,run_id,turn_index,turn_state,input_tokens_used,output_tokens_used,"
		"pending_calls_json,compacted_summary,pipeline_snapshot_json,created_at "
		"FROM run_checkpoints WHERE run_id=? ORDER BY turn_index DESC LIMIT 1",
		-1, &s, nullptr);
	bind_text(s, 1, run_id);
	if (sqlite3_step(s) != SQLITE_ROW) {
		sqlite3_finalize(s);
		return std::nullopt;
	}
	nlohmann::json j{
		{"id", col(s, 0)},
		{"run_id", col(s, 1)},
		{"turn_index", sqlite3_column_int(s, 2)},
		{"turn_state", col(s, 3)},
		{"input_tokens_used", sqlite3_column_int64(s, 4)},
		{"output_tokens_used", sqlite3_column_int64(s, 5)},
		{"pending_calls_json", col(s, 6)},
		{"compacted_summary", col(s, 7)},
		{"pipeline_snapshot_json", col(s, 8)},
		{"created_at", col(s, 9)}
	};
	sqlite3_finalize(s);
	return j.dump();
}

std::vector<std::string> SessionStore::list_checkpoints_json(const std::string& run_id) {
	std::lock_guard lock(mutex_);
	std::vector<std::string> out;
	sqlite3_stmt* s = nullptr;
	sqlite3_prepare_v2(db_,
		"SELECT id,run_id,turn_index,turn_state,input_tokens_used,output_tokens_used,"
		"pending_calls_json,compacted_summary,pipeline_snapshot_json,created_at "
		"FROM run_checkpoints WHERE run_id=? ORDER BY turn_index ASC",
		-1, &s, nullptr);
	bind_text(s, 1, run_id);
	while (sqlite3_step(s) == SQLITE_ROW) {
		nlohmann::json j{
			{"id", col(s, 0)},
			{"run_id", col(s, 1)},
			{"turn_index", sqlite3_column_int(s, 2)},
			{"turn_state", col(s, 3)},
			{"input_tokens_used", sqlite3_column_int64(s, 4)},
			{"output_tokens_used", sqlite3_column_int64(s, 5)},
			{"pending_calls_json", col(s, 6)},
			{"compacted_summary", col(s, 7)},
			{"pipeline_snapshot_json", col(s, 8)},
			{"created_at", col(s, 9)}
		};
		out.push_back(j.dump());
	}
	sqlite3_finalize(s);
	return out;
}

void SessionStore::prune_checkpoints(const std::string& run_id, int keep_latest_n) {
	std::lock_guard lock(mutex_);
	if (keep_latest_n <= 0) {
		// Delete all checkpoints for this run
		sqlite3_stmt* s = nullptr;
		sqlite3_prepare_v2(db_,
			"DELETE FROM run_checkpoints WHERE run_id=?",
			-1, &s, nullptr);
		bind_text(s, 1, run_id);
		sqlite3_step(s);
		sqlite3_finalize(s);
		return;
	}
	// Delete all except the top N by turn_index
	sqlite3_stmt* s = nullptr;
	sqlite3_prepare_v2(db_,
		"DELETE FROM run_checkpoints WHERE run_id=? AND turn_index NOT IN "
		"(SELECT turn_index FROM run_checkpoints WHERE run_id=? "
		"ORDER BY turn_index DESC LIMIT ?)",
		-1, &s, nullptr);
	bind_text(s, 1, run_id);
	bind_text(s, 2, run_id);
	sqlite3_bind_int(s, 3, keep_latest_n);
	sqlite3_step(s);
	sqlite3_finalize(s);
}

} // namespace merak
