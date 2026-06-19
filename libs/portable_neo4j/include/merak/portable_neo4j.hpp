#pragma once
#include <filesystem>
#include <string>

namespace merak {

class PortableNeo4j {
public:
    explicit PortableNeo4j(std::filesystem::path neo4j_dir,
                           std::filesystem::path java_home);
    ~PortableNeo4j();

    PortableNeo4j(const PortableNeo4j&) = delete;
    PortableNeo4j& operator=(const PortableNeo4j&) = delete;

    // Start Neo4j. Returns true on success.
    // Calls neo4j-admin set-initial-password on first run.
    bool start();

    // Stop Neo4j via neo4j stop
    void stop();

    // True if Neo4j is running
    bool is_running() const { return running_; }

    // Bolt port number Neo4j is listening on
    int bolt_port() const { return bolt_port_; }

    // HTTP port number
    int http_port() const { return http_port_; }

    // Bolt URI for Neo4jKGProvider
    std::string bolt_uri() const;

    // Default credentials (set on first run)
    std::string user() const { return "neo4j"; }
    std::string password() const { return password_; }

private:
    std::filesystem::path neo4j_dir_;
    std::filesystem::path java_home_;
    int bolt_port_ = 0;
    int http_port_ = 0;
    bool running_ = false;
    std::string password_ = "merak123";

    std::filesystem::path bin_dir() const { return neo4j_dir_ / "bin"; }
    std::filesystem::path data_dir() const { return neo4j_dir_ / "data"; }
    std::string neo4j_cmd() const;
    bool is_first_run() const;
    bool set_initial_password();
    bool start_server();
    bool wait_ready(int timeout_seconds = 60);
    int find_free_port();
    bool is_port_in_use(int port) const;
};

} // namespace merak
