#pragma once

#include <libpq-fe.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace merak::worldbuilding {

class PgPool {
public:
    PgPool(std::string_view conninfo, int pool_size = 5);
    ~PgPool();

    PgPool(const PgPool&) = delete;
    PgPool& operator=(const PgPool&) = delete;
    PgPool(PgPool&&) = delete;
    PgPool& operator=(PgPool&&) = delete;

    PGconn* acquire();
    void release(PGconn* conn);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class PgConn {
public:
    explicit PgConn(PgPool& pool);
    ~PgConn();

    PgConn(const PgConn&) = delete;
    PgConn& operator=(const PgConn&) = delete;
    PgConn(PgConn&& other) noexcept;
    PgConn& operator=(PgConn&& other) noexcept;

    PGconn* get() const noexcept;

    void exec(std::string_view sql);
    class PgResult query(std::string_view sql,
                         const std::vector<std::string>& params = {});
    int execute(std::string_view sql,
                const std::vector<std::string>& params = {});

private:
    PgPool* pool_ = nullptr;
    PGconn* conn_ = nullptr;
};

class PgResult {
public:
    PgResult() : result_(nullptr) {}
    explicit PgResult(PGresult* result);
    ~PgResult();

    PgResult(const PgResult&) = delete;
    PgResult& operator=(const PgResult&) = delete;
    PgResult(PgResult&& other) noexcept;
    PgResult& operator=(PgResult&& other) noexcept;

    PGresult* get() const noexcept;
    ExecStatusType status() const noexcept;
    int ntuples() const noexcept;
    int nfields() const noexcept;

    std::string get(int row, int col) const;
    bool is_null(int row, int col) const noexcept;

    void check_ok(std::string_view context) const;

private:
    PGresult* result_ = nullptr;
};

} // namespace merak::worldbuilding
