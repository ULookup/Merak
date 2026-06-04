#pragma once
#include <merak/config.hpp>
#include <merak/runtime_service.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <vector>

namespace merak {

struct McpServerStatus { std::string name; bool alive = false; };
struct RuntimeMetadata {
    std::string provider;
    std::string model;
    std::vector<ModelEntry> models;
    std::string permission_mode;
    bool memory_enabled = false;
    std::vector<ToolSpec> tools;
    std::vector<McpServerStatus> mcp_servers;
    std::vector<AgentMetadata> agents;
};
struct HttpResult { int status; nlohmann::json body; };

class HttpServer {
public:
    HttpServer(std::shared_ptr<RuntimeService> runtime, RuntimeMetadata metadata);
    void listen(int port);
    void stop();
    HttpResult handle_runtime_metadata() const;
    HttpResult handle_session_memory(const std::string& id) const;
    HttpResult handle_create_session(const std::string& title = "");
    HttpResult handle_get_session(const std::string& id) const;
    HttpResult handle_create_delegation(
        const std::string& session_id,
        const DelegationRequest& request);
private:
    std::shared_ptr<RuntimeService> runtime_;
    RuntimeMetadata metadata_;
    httplib::Server server_;
    void install_routes();
    static void json(httplib::Response& response, const HttpResult& result);
    static HttpResult error(const std::string& code, const std::string& message,
                            int status, bool retryable = false);
};

} // namespace merak
