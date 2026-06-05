#pragma once
#include <filesystem>
#include <string>

namespace merak {

class PortablePg {
public:
    explicit PortablePg(std::filesystem::path pg_dir);
    ~PortablePg();

    PortablePg(const PortablePg&) = delete;
    PortablePg& operator=(const PortablePg&) = delete;

    // Start PostgreSQL. Returns true on success.
    // If pg_dir_/data doesn't exist or is empty, runs initdb first.
    bool start();

    // Stop PostgreSQL via pg_ctl stop -m fast
    void stop();

    // True if PostgreSQL is running
    bool is_running() const { return running_; }

    // Port number PostgreSQL is listening on
    int port() const { return port_; }

    // Connection string for the merak database (libpq format)
    std::string connection_string() const;

private:
    std::filesystem::path pg_dir_;
    int port_ = 0;
    bool running_ = false;

    std::filesystem::path bin_dir() const { return pg_dir_ / "bin"; }
    std::filesystem::path data_dir() const { return pg_dir_ / "data"; }
    bool initdb();
    bool start_server();
    bool wait_ready(int timeout_seconds = 30);
    bool create_database();
    int find_free_port();
    bool is_port_in_use(int port) const;
};

} // namespace merak
