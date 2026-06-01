#pragma once
#include <merak/tool_spec.hpp>
#include <merak/message.hpp>
#include <merak/config.hpp>
#include <merak/error.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <future>
#include <memory>
#include <expected>

namespace merak {

struct McpProcessInfo {
    std::string server_name;
    int pid = -1;
    bool running = false;
};

class McpClient {
public:
    explicit McpClient(const MCPServerConfig& config);
    ~McpClient();

    std::expected<void, AgentError> connect();
    void disconnect();
    bool is_alive() const;

    std::future<std::expected<std::vector<ToolSpec>, AgentError>> list_tools();
    std::future<ToolResult> call_tool(const ToolCall& call);

    const std::string& server_name() const { return config_.name; }
    McpProcessInfo process_info() const;

private:
    MCPServerConfig config_;
    int stdin_fd_ = -1;
    int stdout_fd_ = -1;
    int child_pid_ = -1;
    std::atomic<int> next_id_{1};
    std::mutex io_mutex_;

    std::future<nlohmann::json> send_request(
        const std::string& method,
        const nlohmann::json& params
    );
    void write_line(const nlohmann::json& msg);
    nlohmann::json read_line();
};

} // namespace merak
