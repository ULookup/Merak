#pragma once
#include <merak/agent_loop.hpp>
#include <merak/runtime_event.hpp>
#include <merak/session_store.hpp>
#include <condition_variable>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <filesystem>
#include <vector>

namespace merak {

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
    using LoopFactory = std::function<std::unique_ptr<AgentLoop>()>;
    explicit RuntimeService(std::filesystem::path root, LoopFactory factory = {});
    void initialize();
    SessionRecord create_session(const std::string& title = "");
    std::vector<SessionRecord> list_sessions() const;
    std::optional<SessionRecord> get_session(const std::string& id) const;
    std::optional<RunRecord> get_run(const std::string& id) const;
    RunRecord create_run_record(const std::string& session_id, const std::string& message);
    RunRecord start_run(const std::string& session_id, const std::string& message);
    ApprovalRecord resolve_approval(const std::string& id, ApprovalStatus status);
    void cancel_run(const std::string& id);
    std::vector<RuntimeEvent> events_after(const std::string& session_id, long long after) const;
    std::shared_ptr<EventSubscription> subscribe(const std::string& session_id);

private:
    class Control;
    SessionStore store_;
    EventBus bus_;
    LoopFactory loop_factory_;
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<CancellationToken>> tokens_;
    std::map<std::string, std::shared_ptr<Control>> controls_;
    RuntimeEvent emit(const std::string& session_id, const std::string& run_id,
                      const std::string& type, nlohmann::json payload = {});
    void execute_run(RunRecord run);
    void resume_after_restarted_approval(
        RunRecord run, ApprovalRecord approval, bool allowed);
    std::vector<Message> restore_messages(const std::string& session_id) const;
};

} // namespace merak
