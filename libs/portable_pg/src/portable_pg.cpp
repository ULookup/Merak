#include <merak/portable_pg.hpp>
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
#define SOCKET SOCKET
#define INVALID_SOCKET INVALID_SOCKET
#define SOCKET_ERROR SOCKET_ERROR
#define closesocket closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

namespace merak {

PortablePg::PortablePg(std::filesystem::path pg_dir) : pg_dir_(std::move(pg_dir)) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

PortablePg::~PortablePg() {
    if (running_) stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool PortablePg::is_port_in_use(int port) const {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) return true;
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    int result = connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    closesocket(sock);
    return result != SOCKET_ERROR;
}

int PortablePg::find_free_port() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(15432, 16431);
    for (int i = 0; i < 100; ++i) {
        int port = dist(gen);
        if (!is_port_in_use(port)) return port;
    }
    return 0;
}

bool PortablePg::initdb() {
    std::filesystem::create_directories(data_dir());
    auto initdb_path = bin_dir() / "initdb";
#ifdef _WIN32
    initdb_path += ".exe";
#endif
    std::ostringstream cmd;
    cmd << "\"" << initdb_path.string() << "\""
        << " -D \"" << data_dir().string() << "\""
        << " --no-locale --encoding=UTF8";
    auto log_path = pg_dir_ / "pg.init.log";
    cmd << " > \"" << log_path.string() << "\" 2>&1";
    int ret = std::system(cmd.str().c_str());
    return ret == 0;
}

bool PortablePg::start_server() {
    port_ = find_free_port();
    if (port_ == 0) return false;

    auto pg_ctl_path = bin_dir() / "pg_ctl";
#ifdef _WIN32
    pg_ctl_path += ".exe";
#endif
    auto log_path = pg_dir_ / "pg.log";
    std::ostringstream cmd;
    cmd << "\"" << pg_ctl_path.string() << "\""
        << " start -D \"" << data_dir().string() << "\""
        << " -l \"" << log_path.string() << "\""
        << " -o \"-p " << port_
        << " -k \\\"" << pg_dir_.string() << "\\\"\""
        << " >> \"" << (pg_dir_ / "pg.init.log").string() << "\" 2>&1";

    int ret = std::system(cmd.str().c_str());
    if (ret != 0) return false;

    std::ofstream port_file(pg_dir_ / "pg.port");
    port_file << port_;
    running_ = true;
    return true;
}

bool PortablePg::wait_ready(int timeout_seconds) {
    for (int i = 0; i < timeout_seconds * 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) continue;
        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(port_));
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        int result = connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
        closesocket(sock);
        if (result != SOCKET_ERROR) return true;
    }
    return false;
}

bool PortablePg::create_database() {
    auto psql_path = bin_dir() / "psql";
#ifdef _WIN32
    psql_path += ".exe";
#endif
    std::ostringstream cmd;
    cmd << "\"" << psql_path.string() << "\""
        << " -h \"" << pg_dir_.string() << "\""
        << " -p " << port_
        << " -U postgres -d postgres"
        << " -c \"CREATE DATABASE merak;\""
        << " >> \"" << (pg_dir_ / "pg.init.log").string() << "\" 2>&1";
    // Database may already exist — that's fine
    std::system(cmd.str().c_str());
    return true;
}

bool PortablePg::start() {
    if (running_) return true;
    if (!std::filesystem::exists(data_dir()) || std::filesystem::is_empty(data_dir())) {
        if (!initdb()) {
            std::cerr << "PortablePg: initdb failed\n";
            return false;
        }
    }
    if (!start_server()) {
        std::cerr << "PortablePg: pg_ctl start failed\n";
        return false;
    }
    if (!wait_ready()) {
        std::cerr << "PortablePg: timed out waiting for PostgreSQL to accept connections\n";
        return false;
    }
    create_database();
    return true;
}

void PortablePg::stop() {
    if (!running_) return;
    auto pg_ctl_path = bin_dir() / "pg_ctl";
#ifdef _WIN32
    pg_ctl_path += ".exe";
#endif
    std::ostringstream cmd;
    cmd << "\"" << pg_ctl_path.string() << "\""
        << " stop -D \"" << data_dir().string() << "\""
#ifdef _WIN32
        << " -m fast > nul 2>&1";
#else
        << " -m fast > /dev/null 2>&1";
#endif
    int ret = std::system(cmd.str().c_str());
    if (ret != 0) {
        std::cerr << "PortablePg: pg_ctl stop returned " << ret << " (postgres may still be running)\n";
    }
    running_ = false;
}

std::string PortablePg::connection_string() const {
    std::ostringstream s;
    s << "host=" << pg_dir_.string()
      << " port=" << port_
      << " dbname=merak user=postgres";
    return s.str();
}

} // namespace merak
