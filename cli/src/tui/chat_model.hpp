#pragma once
#include "composer.hpp"
#include "history_cell.hpp"
#include "status_indicator.hpp"
#include <merak/turn_state.hpp>
#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace merak::tui {

class ChatModel {
    struct AgentStatus {
        std::chrono::steady_clock::time_point started_at = std::chrono::steady_clock::now();
        int steps = 0;
        TurnState state = TurnState::Idle;
        bool complete = false;
        bool failed = false;
        std::chrono::steady_clock::time_point finished_at{};
    };
    std::vector<std::unique_ptr<HistoryCell>> committed_;
    std::unique_ptr<AssistantCell> assistant_;
    std::unique_ptr<ApprovalCell> approval_;
    std::map<std::string, std::unique_ptr<ToolCell>> tools_;
    std::map<std::string, AgentStatus> agents_;
public:
    Composer composer;
    StatusIndicator status;

    void commit(std::unique_ptr<HistoryCell> cell) {
        if (cell && cell->persisted()) committed_.push_back(std::move(cell));
    }
    std::vector<std::unique_ptr<HistoryCell>> drain_committed() {
        std::vector<std::unique_ptr<HistoryCell>> out;
        out.swap(committed_);
        return out;
    }
    void begin_turn() { status.begin_turn(); }
    void append_answer(const std::string& delta) {
        if (!assistant_) assistant_ = std::make_unique<AssistantCell>();
        assistant_->append(delta);
        status.bump_stream_chars(delta.size());
    }
    void finish_answer(const std::string& fallback = "") {
        if (!assistant_ && !fallback.empty()) append_answer(fallback);
        if (!assistant_) return;
        assistant_->finish();
        commit(std::move(assistant_));
    }
    void start_tool(const ToolCall& call) {
        status.set_activity("Running " + call.name);
        tools_[call.id] = std::make_unique<ToolCell>(call);
    }
    void finish_tool(const ToolResult& result, std::chrono::milliseconds duration = std::chrono::milliseconds(0)) {
        auto it = tools_.find(result.call_id);
        if (it == tools_.end()) return;
        it->second->finish(result, duration);
        commit(std::move(it->second));
        tools_.erase(it);
        status.set_activity("");
    }
    void request_approval(const ToolCall& call) { approval_ = std::make_unique<ApprovalCell>(call); }
    void clear_approval() { approval_.reset(); }
    const ApprovalCell* approval() const { return approval_.get(); }
    const AssistantCell* active_assistant() const { return assistant_.get(); }
    void on_agent_started(const std::string& id) { agents_[id] = AgentStatus{}; }
    void on_agent_state(const std::string& id, TurnState state) { agents_[id].state = state; }
    void on_agent_step(const std::string& id) { agents_[id].steps++; }
    void on_agent_ended(const std::string& id, bool failed) {
        auto& agent = agents_[id];
        agent.complete = true;
        agent.failed = failed;
        agent.finished_at = std::chrono::steady_clock::now();
    }
    std::vector<std::string> agent_rows() const {
        std::vector<std::string> out;
        auto now = std::chrono::steady_clock::now();
        for (const auto& [id, agent] : agents_) {
            if (agent.complete && !agent.failed
                && now - agent.finished_at > std::chrono::seconds(5)) continue;
            auto state = agent.failed ? "x" : (agent.complete ? "o" : "*");
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                (agent.complete ? agent.finished_at : now) - agent.started_at);
            out.push_back(std::string(state) + " " + id + " | "
                + std::to_string(agent.steps) + (agent.steps == 1 ? " step" : " steps")
                + " | " + std::to_string(elapsed.count()) + "s");
        }
        return out;
    }
};

} // namespace merak::tui
