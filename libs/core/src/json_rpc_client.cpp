#include <merak/json_rpc_client.hpp>

#include <nlohmann/json.hpp>

#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace merak {

JsonRpcClient::~JsonRpcClient() {
    stop();
}

bool JsonRpcClient::start(const std::string& command, const std::vector<std::string>& args) {
    // If already running, stop first
    if (child_pid_ > 0) {
        stop();
    }

    int to_child[2];   // parent writes → child reads  (stdin)
    int from_child[2]; // child writes → parent reads (stdout)

    if (pipe(to_child) != 0 || pipe(from_child) != 0) {
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        // fork failed
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        return false;
    }

    if (pid == 0) {
        // --- child ---
        // Redirect stdin: read from to_child[0]
        dup2(to_child[0], STDIN_FILENO);
        close(to_child[0]);
        close(to_child[1]);

        // Redirect stdout: write to from_child[1]
        dup2(from_child[1], STDOUT_FILENO);
        close(from_child[0]);
        close(from_child[1]);

        // Build argv (null-terminated)
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(command.c_str()));
        for (const auto& a : args) {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);

        execvp(command.c_str(), argv.data());

        // execvp only returns on error
        _exit(127);
    }

    // --- parent ---
    // Close the ends we don't use
    close(to_child[0]);   // parent doesn't read from child's stdin pipe
    close(from_child[1]); // parent doesn't write to child's stdout pipe

    stdin_fd_  = to_child[1];    // write here → child's stdin
    stdout_fd_ = from_child[0];  // read here  ← child's stdout
    child_pid_ = static_cast<int>(pid);

    next_id_ = 1;

    return true;
}

bool JsonRpcClient::is_alive() const {
    if (child_pid_ <= 0) return false;
    // kill(pid, 0) checks if the process exists without sending a signal
    return kill(child_pid_, 0) == 0;
}

void JsonRpcClient::write_message(const nlohmann::json& msg) {
    std::lock_guard<std::mutex> lock(io_mutex_);

    std::string data = msg.dump() + "\n";

    ssize_t written = 0;
    size_t total = data.size();
    const char* ptr = data.c_str();

    while (written < static_cast<ssize_t>(total)) {
        ssize_t n = write(stdin_fd_, ptr + written, total - static_cast<size_t>(written));
        if (n <= 0) {
            // Write failed — child may have died
            break;
        }
        written += n;
    }
}

nlohmann::json JsonRpcClient::read_message() {
    std::lock_guard<std::mutex> lock(read_mutex_);

    // Use a FILE* wrapper around the fd so we can use getline() conveniently.
    // dup the fd first so closing the FILE* does not close the underlying fd.
    int fd = dup(stdout_fd_);
    if (fd < 0) {
        return nlohmann::json::object();
    }

    FILE* stream = fdopen(fd, "r");
    if (!stream) {
        close(fd);
        return nlohmann::json::object();
    }

    char* line = nullptr;
    size_t len = 0;
    ssize_t nread = getline(&line, &len, stream);

    fclose(stream); // also closes the dup'd fd

    if (nread <= 0) {
        free(line);
        return nlohmann::json::object();
    }

    std::string line_str(line, static_cast<size_t>(nread));

    // Trim trailing whitespace / newline
    while (!line_str.empty() &&
           (line_str.back() == '\n' || line_str.back() == '\r')) {
        line_str.pop_back();
    }

    free(line);

    if (line_str.empty()) {
        return nlohmann::json::object();
    }

    try {
        return nlohmann::json::parse(line_str);
    } catch (const nlohmann::json::exception&) {
        return nlohmann::json::object();
    }
}

std::future<nlohmann::json> JsonRpcClient::send_request(
    const std::string& method,
    const nlohmann::json& params) {

    int id = next_id_.fetch_add(1);

    nlohmann::json req;
    req["jsonrpc"] = "2.0";
    req["id"] = id;
    req["method"] = method;
    req["params"] = params;

    write_message(req);

    return std::async(std::launch::async, [this, id]() -> nlohmann::json {
        // Inner async: the actual read+retry loop (now serialized via read_mutex_)
        auto read_fut = std::async(std::launch::async, [this, id]() -> nlohmann::json {
            nlohmann::json resp = read_message();

            constexpr int kMaxRetries = 10;
            for (int i = 0; i < kMaxRetries; ++i) {
                if (resp.contains("id") && resp["id"] == id) {
                    return resp;
                }
                resp = read_message();
            }

            nlohmann::json err;
            err["jsonrpc"] = "2.0";
            err["id"] = id;
            err["error"] = {{"code", -1}, {"message", "no matching response received"}};
            return err;
        });

        // 30s timeout — prevent indefinite blocking on hung child process
        if (read_fut.wait_for(std::chrono::seconds(30)) == std::future_status::timeout) {
            nlohmann::json err;
            err["jsonrpc"] = "2.0";
            err["id"] = id;
            err["error"] = {{"code", -1}, {"message", "read timeout"}};
            return err;
        }
        return read_fut.get();
    });
}

void JsonRpcClient::send_notification(
    const std::string& method,
    const nlohmann::json& params) {

    nlohmann::json notif;
    notif["jsonrpc"] = "2.0";
    notif["method"] = method;
    notif["params"] = params;
    // deliberately no "id" field

    write_message(notif);
}

void JsonRpcClient::stop() {
    if (child_pid_ > 0) {
        kill(child_pid_, SIGTERM);

        // Wait up to 5 seconds for graceful shutdown
        int status = 0;
        for (int i = 0; i < 50; ++i) {
            pid_t w = waitpid(child_pid_, &status, WNOHANG);
            if (w == child_pid_) break;
            if (w < 0 && errno == ECHILD) break;
            usleep(100000); // 100 ms
        }

        // Force kill if still alive
        if (is_alive()) {
            kill(child_pid_, SIGKILL);
            waitpid(child_pid_, &status, 0);
        }

        child_pid_ = -1;
    }

    if (stdin_fd_ >= 0) {
        close(stdin_fd_);
        stdin_fd_ = -1;
    }
    if (stdout_fd_ >= 0) {
        close(stdout_fd_);
        stdout_fd_ = -1;
    }
}

} // namespace merak
