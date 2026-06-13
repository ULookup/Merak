#pragma once

#include <sqlite3.h>

#include <string>
#include <string_view>

namespace merak::worldbuilding {

class SqliteDb {
public:
    explicit SqliteDb(std::string_view path);
    ~SqliteDb();

    SqliteDb(const SqliteDb&) = delete;
    SqliteDb& operator=(const SqliteDb&) = delete;
    SqliteDb(SqliteDb&& other) noexcept;
    SqliteDb& operator=(SqliteDb&& other) noexcept;

    sqlite3* get() const noexcept;
    void exec(std::string_view sql);

private:
    sqlite3* db_ = nullptr;
};

class Statement {
public:
    Statement(SqliteDb& db, std::string_view sql);
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    sqlite3_stmt* get() const noexcept;
    bool step();
    void reset();

private:
    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_ = nullptr;
};

void bind_text(Statement& statement, int index, std::string_view value);
void bind_int(Statement& statement, int index, int value);
void bind_double(Statement& statement, int index, double value);

std::string column_text(Statement& statement, int index);
int column_int(Statement& statement, int index);
double column_double(Statement& statement, int index);

} // namespace merak::worldbuilding
