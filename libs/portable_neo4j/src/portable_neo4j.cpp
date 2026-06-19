#include <merak/portable_neo4j.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace merak {

// ── cross-platform socket helpers ──────────────────────

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
constexpr int kSocketError = SOCKET_ERROR;
#define closesock closesocket
#else
using Socket = int;
constexpr Socket kInvalidSocket = -1;
constexpr int kSocketError = -1;
#define closesock close
#endif

PortableNeo4j::PortableNeo4j(std::filesystem::path neo4j_dir,
                              std::filesystem::path java_home)
    : neo4j_dir_(std::move(neo4j_dir))
    , java_home_(std::move(java_home))
{
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

PortableNeo4j::~PortableNeo4j() {
    if (running_) stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool PortableNeo4j::is_port_in_use(int port) const {
    Socket sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == kInvalidSocket) return true;
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int result = connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    closesock(sock);
    return result != kSocketError;
}

int PortableNeo4j::find_free_port() {
    std::random_device rd;
    std::mt19937 gen(rd());
    // Neo4j default Bolt: 7687, HTTP: 7474
    // Use a range away from defaults to avoid conflicts
    std::uniform_int_distribution<> dist(17687, 18686);
    for (int i = 0; i < 100; ++i) {
        int port = dist(gen);
        if (!is_port_in_use(port)) return port;
    }
    return 0;
}

std::string PortableNeo4j::neo4j_cmd() const {
    auto cmd = bin_dir() / "neo4j";
#ifdef _WIN32
    cmd += ".bat";
#endif
    return cmd.string();
}

bool PortableNeo4j::is_first_run() const {
    // Neo4j creates data/databases and data/transactions on first start
    return !std::filesystem::exists(data_dir() / "databases") ||
           std::filesystem::is_empty(data_dir() / "databases");
}

bool PortableNeo4j::set_initial_password() {
    // The neo4j-admin set-initial-password command requires the server to be
    // running. We do this after start_server + wait_ready.
    auto admin = bin_dir() / "neo4j-admin";
#ifdef _WIN32
    admin += ".bat";
#endif
    std::ostringstream cmd;
    cmd << "\"" << admin.string() << "\""
        << " dbms set-initial-password \"" << password_ << "\""
#ifdef _WIN32
        << " > nul 2>&1";
#else
        << " > /dev/null 2>&1";
#endif
    int ret = std::system(cmd.str().c_str());
    // "neo4j-admin set-initial-password" only works when the default
    // password ("neo4j"/"neo4j") is still active. If password was already
    // changed, the command returns non-zero — that's fine.
    if (ret != 0) {
        std::cerr << "PortableNeo4j: set-initial-password returned " << ret
                  << " (password may already be set)\n";
    }
    return true;
}

bool PortableNeo4j::start_server() {
    bolt_port_ = find_free_port();
    http_port_ = find_free_port();
    if (bolt_port_ == 0 || http_port_ == 0) return false;

    // Ensure http_port_ is different from bolt_port_
    while (http_port_ == bolt_port_) {
        http_port_ = find_free_port();
        if (http_port_ == 0) return false;
    }

    auto log_path = neo4j_dir_ / "neo4j.log";
    std::ostringstream cmd;

#ifdef _WIN32
    // On Windows, use neo4j.bat console with env vars for port override.
    // Neo4j reads dbms.connector.bolt.listen_address from conf or env.
    cmd << "set NEO4J_HOME=" << neo4j_dir_.string() << " && "
        << "set JAVA_HOME=" << java_home_.string() << " && "
        << "\"" << neo4j_cmd() << "\""
        << " console"
        << " > \"" << log_path.string() << "\" 2>&1";
#else
    cmd << "NEO4J_HOME=\"" << neo4j_dir_.string() << "\" "
        << "JAVA_HOME=\"" << java_home_.string() << "\" "
        << "\"" << neo4j_cmd() << "\""
        << " console"
        << " > \"" << log_path.string() << "\" 2>&1 &";
#endif

    // Before starting, write a minimal neo4j.conf that overrides ports.
    auto conf_dir = neo4j_dir_ / "conf";
    std::filesystem::create_directories(conf_dir);
    auto conf_path = conf_dir / "neo4j.conf";
    std::ofstream conf(conf_path, std::ios::app); // append, don't clobber
    if (conf) {
        conf << "\n# PortableNeo4j override\n";
        conf << "server.bolt.listen_address=:" << bolt_port_ << "\n";
        conf << "server.http.listen_address=:" << http_port_ << "\n";
        conf << "server.bolt.advertised_address=:" << bolt_port_ << "\n";
        conf << "server.http.advertised_address=:" << http_port_ << "\n";
        conf << "dbms.security.auth_enabled=true\n";
        conf.close();
    }

    int ret = std::system(cmd.str().c_str());
    if (ret != 0) {
        std::cerr << "PortableNeo4j: neo4j console returned " << ret << "\n";
        return false;
    }

    running_ = true;
    return true;
}

bool PortableNeo4j::wait_ready(int timeout_seconds) {
    for (int i = 0; i < timeout_seconds * 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        Socket sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == kInvalidSocket) continue;
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<unsigned short>(bolt_port_));
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int result = connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        closesock(sock);
        if (result != kSocketError) return true;
    }
    return false;
}

bool PortableNeo4j::start() {
    if (running_) return true;

    if (!std::filesystem::exists(neo4j_dir_)) {
        std::cerr << "PortableNeo4j: Neo4j directory not found at "
                  << neo4j_dir_ << "\n";
        return false;
    }
    if (!std::filesystem::exists(java_home_ / "bin" / "java") &&
        !std::filesystem::exists(java_home_ / "bin" / "java.exe")) {
        std::cerr << "PortableNeo4j: Java not found at "
                  << java_home_ << "\n";
        return false;
    }

    if (!start_server()) {
        std::cerr << "PortableNeo4j: failed to start Neo4j server\n";
        return false;
    }
    if (!wait_ready()) {
        std::cerr << "PortableNeo4j: timed out waiting for Neo4j Bolt port "
                  << bolt_port_ << "\n";
        stop();
        return false;
    }

    // Set initial password on first run
    if (is_first_run()) {
        // Small delay to let the server fully initialize
        std::this_thread::sleep_for(std::chrono::seconds(2));
        set_initial_password();
    }

    return true;
}

void PortableNeo4j::stop() {
    if (!running_) return;

    std::ostringstream cmd;
#ifdef _WIN32
    cmd << "set NEO4J_HOME=" << neo4j_dir_.string() << " && "
        << "\"" << neo4j_cmd() << "\" stop"
        << " > nul 2>&1";
#else
    cmd << "NEO4J_HOME=\"" << neo4j_dir_.string() << "\" "
        << "\"" << neo4j_cmd() << "\" stop"
        << " > /dev/null 2>&1";
#endif
    int ret = std::system(cmd.str().c_str());
    if (ret != 0) {
        std::cerr << "PortableNeo4j: neo4j stop returned " << ret
                  << " (server may still be running)\n";
    }
    running_ = false;
}

std::string PortableNeo4j::bolt_uri() const {
    std::ostringstream s;
    s << "bolt://localhost:" << bolt_port_;
    return s.str();
}

} // namespace merak
