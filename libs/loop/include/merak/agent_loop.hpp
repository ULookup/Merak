#pragma once
#include <merak/message.hpp>
#include <merak/turn_state.hpp>
#include <merak/config.hpp>
#include <merak/error.hpp>
#include <merak/llm_provider.hpp>
#include <merak/tool_registry.hpp>
#include <merak/memory_store.hpp>
#include <merak/context_assembler.hpp>
#include <merak/compactor.hpp>
#include <merak/cache_aware_context.hpp>
#include <atomic>
#include <functional>
#include <future>
#include <memory>

namespace merak {

class AgentLoop {
public:
    struct Config {
        int max_turns = 25;
        std::string system_prompt;
        std::string default_model = "gpt-4o";
        bool enable_compaction = true;
        bool enable_cache = true;
    };

    struct Callbacks {
        std::function<void(std::string)> on_text_delta;
        std::function<void(ToolCall)> on_tool_start;
        std::function<void(ToolResult)> on_tool_end;
        std::function<void(TurnState, TurnState)> on_state_change;
        std::function<bool(ToolCall)> on_permission_ask;
        std::function<void(int, int, bool)> on_usage;
    };

    AgentLoop(
        Config config,
        std::shared_ptr<LlmProvider> llm,
        std::shared_ptr<ToolRegistry> tools,
        std::shared_ptr<MemoryStore> memory,
        std::shared_ptr<ContextAssembler> context,
        std::shared_ptr<Compactor> compactor
    );

    std::future<AgentResponse> run(const std::string& user_message);
    TurnState current_state() const { return state_; }
    void set_callbacks(Callbacks cbs) { callbacks_ = std::move(cbs); }
    std::shared_ptr<ToolRegistry> tools() { return tools_; }
    void request_cancel() { cancel_requested_.store(true); }
    void reset_cancel() { cancel_requested_.store(false); }
    bool cancel_requested() const { return cancel_requested_.load(); }

private:
    Config config_;
    TurnState state_ = TurnState::Idle;
    Callbacks callbacks_;

    std::shared_ptr<LlmProvider> llm_;
    std::shared_ptr<ToolRegistry> tools_;
    std::shared_ptr<MemoryStore> memory_;
    std::shared_ptr<ContextAssembler> context_;
    std::shared_ptr<Compactor> compactor_;
    std::shared_ptr<TokenCounter> counter_;

    std::vector<Message> session_history_;
    std::map<std::string, int> tool_failure_streak_;
    std::atomic<bool> cancel_requested_{false};
    static constexpr int kCircuitBreakerThreshold = 3;

    void transition_to(TurnState next);
    std::vector<Message> build_context(const std::string& user_message);
    std::vector<ToolResult> handle_tool_calls(
        const std::vector<ToolCall>& calls
    );
    void maybe_compact();
    bool stop_if_cancelled(AgentResponse& response);
};

} // namespace merak
