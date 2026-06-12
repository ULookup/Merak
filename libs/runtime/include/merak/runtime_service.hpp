#pragma once
#include <merak/agent_loop.hpp>
#include <merak/skills/skill_registry.hpp>
#include <merak/runtime_event.hpp>
#include <merak/session_store.hpp>
#include <merak/prompts/types.hpp>
#include <condition_variable>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <optional>
#include <stdexcept>
#include <filesystem>
#include <vector>

namespace merak::worldbuilding { class WorldbuildingService; class PipelineManager; }

namespace merak {

struct AgentMetadata {
    std::string id;
    std::string description;
};

struct DelegationRequest {
    std::string pattern = "fan_out";
    std::vector<std::string> agent_ids;
    std::string task;
    std::string aggregation = "all_results";
};

struct DelegationStart {
    std::string delegation_id;
    std::string parent_run_id;
    std::string session_id;
};

class RuntimeError : public std::runtime_error {
public:
    RuntimeError(std::string code, std::string message, bool retryable = false)
        : std::runtime_error(std::move(message)), code_(std::move(code)), retryable_(retryable) {}
    const std::string& code() const { return code_; }
    bool retryable() const { return retryable_; }
private:
    std::string code_;
    bool retryable_;
};

class EventSubscription {
public:
    bool wait_next(RuntimeEvent& event, std::chrono::milliseconds timeout);
    void push(const RuntimeEvent& event);
    void close();
private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<RuntimeEvent> events_;
    bool closed_ = false;
};

class EventBus {
public:
    std::shared_ptr<EventSubscription> subscribe(const std::string& session_id);
    void publish(const RuntimeEvent& event);
private:
    std::mutex mutex_;
    std::map<std::string, std::vector<std::weak_ptr<EventSubscription>>> subscriptions_;
};

class RuntimeService : public std::enable_shared_from_this<RuntimeService> {
public:
    using LoopFactory = std::function<std::unique_ptr<AgentLoop>(const std::string& model)>;
    using SubRunExecutor = std::function<AgentResponse(
        const SubAgentConfig& agent, const std::string& task, RunControl& control)>;
    explicit RuntimeService(
        std::filesystem::path root,
        LoopFactory factory = {},
        std::map<std::string, SubAgentConfig> agents = {},
        SubRunExecutor sub_run_executor = {});
    void initialize();
    SessionRecord create_session(const std::string& title = "",
                                  const std::string& world_id = "",
                                  const std::string& agent_id = "");
    void update_session(const std::string& id, const std::string& title);
    SessionRecord archive_session(const std::string& id, bool archived);
    std::string generate_title(const std::string& session_id);
    std::vector<SessionRecord> list_sessions(const std::string& world_id = "") const;
    std::optional<SessionRecord> get_session(const std::string& id) const;
    std::optional<RunRecord> get_run(const std::string& id) const;
    RunRecord create_run_record(const std::string& session_id, const std::string& message);
    RunRecord start_run(const std::string& session_id, const std::string& message,
                        const std::string& model = "");
    DelegationStart start_delegation(
        const std::string& session_id,
        const DelegationRequest& request);
    std::vector<AgentMetadata> agents() const;
    ApprovalRecord resolve_approval(const std::string& id, ApprovalStatus status);
    void set_worldbuilding_service(merak::worldbuilding::WorldbuildingService* wb_service);
    void set_skill_registry(std::shared_ptr<skills::SkillRegistry> reg);
    void set_pipeline_manager(std::shared_ptr<merak::worldbuilding::PipelineManager> mgr);
    merak::worldbuilding::PipelineManager* pipeline_manager() { return pipeline_mgr_.get(); }
    nlohmann::json resolve_creation(const std::string& creation_id,
                                    const std::string& decision,
                                    const nlohmann::json& modifications);
    void cancel_run(const std::string& id);
    void respond_to_ask_user(const std::string& run_id, const std::string& call_id, const std::string& response);
    std::vector<RuntimeEvent> events_after(const std::string& session_id, long long after) const;
    std::shared_ptr<EventSubscription> subscribe(const std::string& session_id);
    RuntimeEvent emit_event(const std::string& session_id, const std::string& run_id,
                            const std::string& type, nlohmann::json payload = {});
    void broadcast_to_world(const std::string& world_id, RuntimeEvent event);
    void register_session_world(const std::string& session_id, const std::string& world_id);
    void unregister_session_world(const std::string& session_id);
    size_t world_session_count(const std::string& world_id) const;

private:
    class Control;
    SessionStore store_;
    EventBus bus_;
    LoopFactory loop_factory_;
    std::map<std::string, SubAgentConfig> agents_;
    SubRunExecutor sub_run_executor_;
    mutable std::mutex mutex_;
    std::map<std::string, std::set<std::string>> world_sessions_;  // world_id -> session_ids
    std::map<std::string, std::string> session_world_;              // session_id -> world_id
    mutable std::mutex world_sessions_mutex_;
    std::map<std::string, std::shared_ptr<CancellationToken>> tokens_;
    std::map<std::string, std::shared_ptr<Control>> controls_;
    std::map<std::string, std::vector<std::string>> child_runs_;
    std::map<std::string, std::shared_ptr<AgentLoop>> session_loops_;
    std::mutex session_loops_mutex_;
    merak::worldbuilding::WorldbuildingService* wb_service_ = nullptr;
    std::shared_ptr<skills::SkillRegistry> skill_registry_;
    std::shared_ptr<merak::worldbuilding::PipelineManager> pipeline_mgr_;
    RuntimeEvent emit(const std::string& session_id, const std::string& run_id,
                      const std::string& type, nlohmann::json payload = {});
    void execute_run(RunRecord run, std::string model);
    prompts::PromptProfile build_prompt_profile(
        const std::string& world_id, const std::string& agent_id);
    void after_entity_event(const std::string& world_id,
                            const std::string& event_type,
                            const nlohmann::json& payload);
    void execute_delegation(RunRecord parent, DelegationRequest request,
                            std::string delegation_id);
    AgentResponse execute_sub_run(
        const SubAgentConfig& agent,
        const std::string& task,
        const RunRecord& parent,
        const std::string& delegation_id,
        std::optional<std::string> previous_output);
    void resume_after_restarted_approval(
        RunRecord run, ApprovalRecord approval, bool allowed);
    std::vector<Message> restore_messages(const std::string& session_id) const;
    static std::string extract_title(const std::string& message, size_t max_len = 50);
};

} // namespace merak
