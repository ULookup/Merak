#include <merak/worldbuilding/sqlite_helpers.hpp>

#include <stdexcept>

namespace merak::worldbuilding {
namespace {

std::runtime_error sqlite_error(sqlite3* db, std::string_view operation) {
    std::string message{operation};
    message += ": ";
    message += db ? sqlite3_errmsg(db) : "sqlite handle is null";
    return std::runtime_error(message);
}

void check(sqlite3* db, int rc, std::string_view operation) {
    if (rc != SQLITE_OK) {
        throw sqlite_error(db, operation);
    }
}

} // namespace

SqliteDb::SqliteDb(std::string_view path) {
    const std::string path_string{path};
    if (sqlite3_open(path_string.c_str(), &db_) != SQLITE_OK) {
        auto error = sqlite_error(db_, "open sqlite database");
        sqlite3_close(db_);
        db_ = nullptr;
        throw error;
    }
}

SqliteDb::~SqliteDb() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
    }
}

SqliteDb::SqliteDb(SqliteDb&& other) noexcept : db_(other.db_) {
    other.db_ = nullptr;
}

SqliteDb& SqliteDb::operator=(SqliteDb&& other) noexcept {
    if (this != &other) {
        if (db_ != nullptr) {
            sqlite3_close(db_);
        }
        db_ = other.db_;
        other.db_ = nullptr;
    }
    return *this;
}

sqlite3* SqliteDb::get() const noexcept { return db_; }

void SqliteDb::exec(std::string_view sql) {
    char* error = nullptr;
    const std::string sql_string{sql};
    const int rc = sqlite3_exec(db_, sql_string.c_str(), nullptr, nullptr, &error);
    if (rc != SQLITE_OK) {
        std::string message = "execute sqlite statement: ";
        message += error != nullptr ? error : sqlite3_errmsg(db_);
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}

Statement::Statement(SqliteDb& db, std::string_view sql) : db_(db.get()) {
    const std::string sql_string{sql};
    check(db_, sqlite3_prepare_v2(db_, sql_string.c_str(), -1, &stmt_, nullptr),
          "prepare sqlite statement");
}

Statement::~Statement() {
    if (stmt_ != nullptr) {
        sqlite3_finalize(stmt_);
    }
}

Statement::Statement(Statement&& other) noexcept
    : db_(other.db_), stmt_(other.stmt_) {
    other.db_ = nullptr;
    other.stmt_ = nullptr;
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
        }
        db_ = other.db_;
        stmt_ = other.stmt_;
        other.db_ = nullptr;
        other.stmt_ = nullptr;
    }
    return *this;
}

sqlite3_stmt* Statement::get() const noexcept { return stmt_; }

bool Statement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    throw sqlite_error(db_, "step sqlite statement");
}

void Statement::reset() {
    check(db_, sqlite3_reset(stmt_), "reset sqlite statement");
    check(db_, sqlite3_clear_bindings(stmt_), "clear sqlite statement bindings");
}

void bind_text(Statement& statement, int index, std::string_view value) {
    check(sqlite3_db_handle(statement.get()),
          sqlite3_bind_text(statement.get(), index, value.data(),
                            static_cast<int>(value.size()), SQLITE_TRANSIENT),
          "bind sqlite text");
}

void bind_int(Statement& statement, int index, int value) {
    check(sqlite3_db_handle(statement.get()),
          sqlite3_bind_int(statement.get(), index, value), "bind sqlite int");
}

void bind_double(Statement& statement, int index, double value) {
    check(sqlite3_db_handle(statement.get()),
          sqlite3_bind_double(statement.get(), index, value),
          "bind sqlite double");
}

std::string column_text(Statement& statement, int index) {
    const auto* text = sqlite3_column_text(statement.get(), index);
    return text == nullptr ? std::string{} :
                             std::string{reinterpret_cast<const char*>(text)};
}

int column_int(Statement& statement, int index) {
    return sqlite3_column_int(statement.get(), index);
}

double column_double(Statement& statement, int index) {
    return sqlite3_column_double(statement.get(), index);
}

} // namespace merak::worldbuilding
