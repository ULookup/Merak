#include <merak/worldbuilding/pg_helpers.hpp>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <stdexcept>

namespace merak::worldbuilding {

// ─── PgPool ────────────────────────────────────────────────────────

struct PgPool::Impl {
    std::string conninfo;
    int pool_size;
    std::queue<PGconn*> available;
    std::mutex mutex;
    std::condition_variable cv;

    Impl(std::string_view info, int size)
        : conninfo(info), pool_size(size) {}
};

PgPool::PgPool(std::string_view conninfo, int pool_size)
    : impl_(std::make_unique<Impl>(conninfo, pool_size)) {
    for (int i = 0; i < pool_size; ++i) {
        PGconn* conn = PQconnectdb(impl_->conninfo.c_str());
        if (PQstatus(conn) != CONNECTION_OK) {
            std::string err = PQerrorMessage(conn);
            PQfinish(conn);
            throw std::runtime_error("PgPool: connection failed: " + err);
        }
        impl_->available.push(conn);
    }
}

PgPool::~PgPool() {
    while (!impl_->available.empty()) {
        PQfinish(impl_->available.front());
        impl_->available.pop();
    }
}

PGconn* PgPool::acquire() {
    std::unique_lock lock(impl_->mutex);
    impl_->cv.wait(lock, [this] { return !impl_->available.empty(); });
    PGconn* conn = impl_->available.front();
    impl_->available.pop();
    if (PQstatus(conn) != CONNECTION_OK) {
        PQreset(conn);
        if (PQstatus(conn) != CONNECTION_OK) {
            std::string err = PQerrorMessage(conn);
            PQfinish(conn);
            conn = PQconnectdb(impl_->conninfo.c_str());
            if (PQstatus(conn) != CONNECTION_OK) {
                std::string err2 = PQerrorMessage(conn);
                PQfinish(conn);
                throw std::runtime_error("PgPool: reconnect failed: " + err2);
            }
        }
    }
    return conn;
}

void PgPool::release(PGconn* conn) {
    if (!conn) return;
    std::lock_guard lock(impl_->mutex);
    impl_->available.push(conn);
    impl_->cv.notify_one();
}

// ─── PgConn ────────────────────────────────────────────────────────

PgConn::PgConn(PgPool& pool) : pool_(&pool), conn_(pool.acquire()) {}

PgConn::~PgConn() {
    if (pool_ && conn_) {
        pool_->release(conn_);
    }
}

PgConn::PgConn(PgConn&& other) noexcept
    : pool_(other.pool_), conn_(other.conn_) {
    other.pool_ = nullptr;
    other.conn_ = nullptr;
}

PgConn& PgConn::operator=(PgConn&& other) noexcept {
    if (this != &other) {
        if (pool_ && conn_) pool_->release(conn_);
        pool_ = other.pool_;
        conn_ = other.conn_;
        other.pool_ = nullptr;
        other.conn_ = nullptr;
    }
    return *this;
}

PGconn* PgConn::get() const noexcept { return conn_; }

void PgConn::exec(std::string_view sql) {
    std::string sql_str(sql);
    PgResult result(PQexec(conn_, sql_str.c_str()));
    result.check_ok("PgConn::exec");
}

PgResult PgConn::query(std::string_view sql,
                       const std::vector<std::string>& params) {
    std::string sql_str(sql);
    std::vector<const char*> values;
    std::vector<int> lengths;
    std::vector<int> formats;
    for (const auto& p : params) {
        values.push_back(p.c_str());
        lengths.push_back(static_cast<int>(p.size()));
        formats.push_back(0); // text
    }
    PgResult result(PQexecParams(
        conn_, sql_str.c_str(),
        static_cast<int>(params.size()),
        nullptr,       // paramTypes — infer from context
        values.data(),
        lengths.data(),
        formats.data(),
        0              // text result format
    ));
    result.check_ok("PgConn::query");
    return result;
}

int PgConn::execute(std::string_view sql,
                    const std::vector<std::string>& params) {
    auto result = query(sql, params);
    std::string tuples = PQcmdTuples(result.get());
    return tuples.empty() ? 0 : std::stoi(tuples);
}

// ─── PgResult ──────────────────────────────────────────────────────

PgResult::PgResult(PGresult* result) : result_(result) {}

PgResult::~PgResult() {
    if (result_) PQclear(result_);
}

PgResult::PgResult(PgResult&& other) noexcept : result_(other.result_) {
    other.result_ = nullptr;
}

PgResult& PgResult::operator=(PgResult&& other) noexcept {
    if (this != &other) {
        if (result_) PQclear(result_);
        result_ = other.result_;
        other.result_ = nullptr;
    }
    return *this;
}

PGresult* PgResult::get() const noexcept { return result_; }

ExecStatusType PgResult::status() const noexcept {
    return PQresultStatus(result_);
}

int PgResult::ntuples() const noexcept { return PQntuples(result_); }
int PgResult::nfields() const noexcept { return PQnfields(result_); }

std::string PgResult::get(int row, int col) const {
    if (PQgetisnull(result_, row, col)) return {};
    return std::string(PQgetvalue(result_, row, col),
                       PQgetlength(result_, row, col));
}

bool PgResult::is_null(int row, int col) const noexcept {
    return PQgetisnull(result_, row, col) != 0;
}

void PgResult::check_ok(std::string_view context) const {
    ExecStatusType s = PQresultStatus(result_);
    if (s != PGRES_COMMAND_OK && s != PGRES_TUPLES_OK) {
        std::string msg(context);
        msg += ": ";
        msg += PQresultErrorMessage(result_);
        throw std::runtime_error(msg);
    }
}

} // namespace merak::worldbuilding
