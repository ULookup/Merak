#pragma once
#include <nlohmann/json.hpp>
#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace merak::client {

struct SseFrame { long long seq=0; std::string type; nlohmann::json payload; };
std::optional<SseFrame> parse_sse_frame(const std::string& frame);
std::string events_stream_url(const std::string& server,const std::string&session,long long after);
std::string delegations_path(const std::string& session);

class RuntimeClient {
public:
    explicit RuntimeClient(std::string server);
    nlohmann::json metadata();
    nlohmann::json create_session(const std::string& title = "");
    nlohmann::json list_sessions();
    nlohmann::json session(const std::string& id);
    nlohmann::json events(const std::string& id,long long after = 0);
    nlohmann::json memory(const std::string& id);
    nlohmann::json start_run(const std::string& id,const std::string& message,
                             const std::string& model = "");
    nlohmann::json start_delegation(
        const std::string& id,
        const std::string& pattern,
        const std::vector<std::string>& agents,
        const std::string& task,
        const std::string& aggregation = "all_results");
    nlohmann::json resolve_approval(const std::string& id,bool allow);
    nlohmann::json cancel_run(const std::string& id);
    void stream_events(const std::string& session,long long after,
                       const std::function<void(SseFrame)>& on_event,
                       std::atomic<bool>& stop);
private:
    std::string server_;
    nlohmann::json request(const std::string& method,const std::string& path,
                           const nlohmann::json& body = {});
};
} // namespace merak::client
