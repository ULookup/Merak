#pragma once
#include <merak/config.hpp>
#include <merak/runtime_service.hpp>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <memory>

namespace merak {

class LlmProvider;

struct McpServerStatus { std::string name; bool alive = false; };
struct RuntimeMetadata {
    std::string provider;
    std::string model;
    std::vector<ModelEntry> models;
    std::string permission_mode;
    bool memory_enabled = false;
    bool worldbuilding_enabled = false;
    nlohmann::json tui_theme = nlohmann::json::object();
    std::vector<ToolSpec> tools;
    std::vector<McpServerStatus> mcp_servers;
    std::vector<AgentMetadata> agents;
};
struct HttpResult { int status; nlohmann::json body; };

struct HttpLimits {
    size_t max_body_size = 10 * 1024 * 1024;  // 10 MB
    int max_requests_per_minute = 120;
};

class HttpServer {
public:
    HttpServer(std::shared_ptr<RuntimeService> runtime, RuntimeMetadata metadata,
               std::string merak_home = "",
               std::shared_ptr<LlmProvider> llm_provider = nullptr);
    void serve_static_dir(const std::string& mount_point, const std::string& dir_path);
    void listen(int port);
    void stop();
    httplib::Server& raw_server() { return server_; }
    HttpResult handle_runtime_metadata() const;
    HttpResult handle_session_memory(const std::string& id) const;
    HttpResult handle_create_session(const std::string& title = "", const std::string& world_id = "", const std::string& agent_id = "");
    HttpResult handle_get_session(const std::string& id) const;
    HttpResult handle_update_session(const std::string& id, const std::string& title);
    HttpResult handle_archive_session(const std::string& id, bool archived);
    HttpResult handle_run_detail(const std::string& id) const;
    HttpResult handle_create_delegation(
        const std::string& session_id,
        const DelegationRequest& request);
private:
    std::shared_ptr<RuntimeService> runtime_;
    RuntimeMetadata metadata_;
    httplib::Server server_;
    std::string merak_home_path_;
    void install_routes();
    void handle_config_get(const httplib::Request& req, httplib::Response& res);
    void handle_config_set(const httplib::Request& req, httplib::Response& res);
    void handle_config_test(const httplib::Request& req, httplib::Response& res);
    void handle_preferences_get(const httplib::Request& req, httplib::Response& res);
    void handle_preferences_set(const httplib::Request& req, httplib::Response& res);
    void handle_workspace_open(const httplib::Request& req, httplib::Response& res);
    void handle_capabilities(const httplib::Request& req, httplib::Response& res);
    void handle_workspace_files_list(const httplib::Request& req, httplib::Response& res);
    void handle_workspace_file_content_get(const httplib::Request& req, httplib::Response& res);
    void handle_workspace_file_content_put(const httplib::Request& req, httplib::Response& res);
    void handle_workspace_file_create(const httplib::Request&, httplib::Response&);
    void handle_workspace_file_delete(const httplib::Request&, httplib::Response&);
    void handle_workspace_file_rename(const httplib::Request&, httplib::Response&);

    HttpLimits limits_;
    std::map<std::string, std::deque<std::chrono::steady_clock::time_point>> rate_buckets_;
    std::mutex rate_mutex_;
    std::shared_ptr<LlmProvider> llm_provider_;
    nlohmann::json cached_config_;
    static void json(httplib::Response& response, const HttpResult& result);
    static HttpResult error(const std::string& code, const std::string& message,
                            int status, bool retryable = false);
};

} // namespace merak
