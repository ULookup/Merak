#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <future>
#include <memory>

namespace merak {

class JsonRpcClient {
public:
    JsonRpcClient() = default;
    virtual ~JsonRpcClient();

    // Start a child process and connect via stdio pipes
    bool start(const std::string& command, const std::vector<std::string>& args);

    // Check if the child process is still alive
    bool is_alive() const;

    // Send a JSON-RPC 2.0 request and return an async future with the response
    std::future<nlohmann::json> send_request(
        const std::string& method,
        const nlohmann::json& params);

    // Send a JSON-RPC notification (no response expected)
    void send_notification(const std::string& method, const nlohmann::json& params);

    // Stop / terminate the child process
    void stop();

protected:
    // Write a JSON message as a single compact line + newline to child stdin
    void write_message(const nlohmann::json& msg);

    // Read a single JSON message line from child stdout
    nlohmann::json read_message();

private:
    int stdin_fd_  = -1;   // parent writes to child's stdin
    int stdout_fd_ = -1;   // parent reads  from child's stdout
    int child_pid_ = -1;

    std::atomic<int> next_id_{1};
    std::mutex io_mutex_;
    std::mutex read_mutex_;  // serializes concurrent read_message calls
};

} // namespace merak
