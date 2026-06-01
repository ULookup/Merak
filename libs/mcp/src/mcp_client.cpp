#include <merak/mcp_client.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sstream>
#include <expected>

namespace merak {

McpClient::McpClient(const MCPServerConfig& config)
    : config_(config)
{
}

McpClient::~McpClient() {
    disconnect();
}

std::expected<void, AgentError> McpClient::connect() {
    int to_child[2];
    int from_child[2];

    if (pipe(to_child) < 0 || pipe(from_child) < 0) {
        return std::unexpected(AgentError(
            ErrorType::MCP_ERROR, "Failed to create pipes"
        ));
    }

    pid_t pid = fork();
    if (pid < 0) {
        return std::unexpected(AgentError(
            ErrorType::MCP_ERROR, "Failed to fork"
        ));
    }

    if (pid == 0) {
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);

        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);

        std::vector<const char*> argv;
        argv.push_back(config_.command.c_str());
        for (auto& arg : config_.args) {
            argv.push_back(arg.c_str());
        }
        argv.push_back(nullptr);

        for (auto& [key, val] : config_.env) {
            setenv(key.c_str(), val.c_str(), 1);
        }

        execvp(config_.command.c_str(), const_cast<char* const*>(argv.data()));
        _exit(1);
    }

    child_pid_ = pid;
    stdin_fd_ = to_child[1];
    stdout_fd_ = from_child[0];

    close(to_child[0]);
    close(from_child[1]);

    nlohmann::json init_params;
    init_params["protocolVersion"] = "2024-11-05";
    init_params["capabilities"] = nlohmann::json::object();
    init_params["clientInfo"]["name"] = "merak-agent";
    init_params["clientInfo"]["version"] = "0.1.0";

    auto resp_future = send_request("initialize", init_params);
    auto resp = resp_future.get();

    if (resp.contains("error")) {
        return std::unexpected(AgentError(
            ErrorType::MCP_ERROR,
            "MCP initialize failed: " + resp["error"]["message"].get<std::string>()
        ));
    }

    spdlog::info("MCP: connected to {}", config_.name);
    return {};
}

void McpClient::disconnect() {
    if (stdin_fd_ >= 0) {
        close(stdin_fd_);
        stdin_fd_ = -1;
    }
    if (stdout_fd_ >= 0) {
        close(stdout_fd_);
        stdout_fd_ = -1;
    }
    if (child_pid_ > 0) {
        kill(child_pid_, SIGTERM);
        waitpid(child_pid_, nullptr, 0);
        child_pid_ = -1;
    }
}

bool McpClient::is_alive() const {
    if (child_pid_ <= 0) return false;
    int status;
    return waitpid(child_pid_, &status, WNOHANG) == 0;
}

std::future<std::expected<std::vector<ToolSpec>, AgentError>>
McpClient::list_tools() {
    return std::async(std::launch::async, [this]()
        -> std::expected<std::vector<ToolSpec>, AgentError>
    {
        auto resp_future = send_request("tools/list", nlohmann::json::object());
        auto resp = resp_future.get();

        if (resp.contains("error")) {
            return std::unexpected(AgentError(
                ErrorType::MCP_ERROR,
                "tools/list failed: " + resp["error"]["message"].get<std::string>()
            ));
        }

        std::vector<ToolSpec> tools;
        for (auto& t : resp["result"]["tools"]) {
            ToolSpec spec;
            spec.name = t["name"].get<std::string>();
            spec.description = t.value("description", "");
            spec.parameters_json = t.value("inputSchema", nlohmann::json::object()).dump();
            spec.source = "mcp://" + config_.name;
            tools.push_back(std::move(spec));
        }

        spdlog::info("MCP {}: listed {} tools", config_.name, tools.size());
        return tools;
    });
}

std::future<ToolResult> McpClient::call_tool(const ToolCall& call) {
    return std::async(std::launch::async, [this, call]() -> ToolResult {
        ToolResult result;
        result.call_id = call.id;

        nlohmann::json params;
        params["name"] = call.name;
        params["arguments"] = nlohmann::json::parse(call.arguments);

        auto resp_future = send_request("tools/call", params);
        auto resp = resp_future.get();

        if (resp.contains("error")) {
            result.is_error = true;
            result.output = resp["error"]["message"].get<std::string>();
        } else {
            std::ostringstream oss;
            for (auto& c : resp["result"]["content"]) {
                if (c.value("type", "") == "text") {
                    oss << c.value("text", "");
                }
            }
            result.output = oss.str();
        }

        return result;
    });
}

std::future<nlohmann::json> McpClient::send_request(
    const std::string& method,
    const nlohmann::json& params
) {
    return std::async(std::launch::async, [this, method, params]() -> nlohmann::json {
        std::lock_guard<std::mutex> lock(io_mutex_);
        int id = next_id_++;

        nlohmann::json request;
        request["jsonrpc"] = "2.0";
        request["id"] = id;
        request["method"] = method;
        request["params"] = params;

        write_line(request);
        return read_line();
    });
}

void McpClient::write_line(const nlohmann::json& msg) {
    std::string line = msg.dump() + "\n";
    write(stdin_fd_, line.c_str(), line.size());
}

nlohmann::json McpClient::read_line() {
    char buf[65536];
    std::string line;
    while (true) {
        ssize_t n = read(stdout_fd_, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = '\0';
        line += buf;

        size_t pos = line.find('\n');
        if (pos != std::string::npos) {
            line = line.substr(0, pos);
            break;
        }
    }
    return nlohmann::json::parse(line);
}

McpProcessInfo McpClient::process_info() const {
    return {config_.name, child_pid_, is_alive()};
}

} // namespace merak
