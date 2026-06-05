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
#include <merak/tool_result_compactor.hpp>
#include <merak/execution.hpp>
#include <future>
#include <memory>

namespace merak {

class AgentLoop {
public:
    struct Config {
        int max_turns = 25;
        std::string system_prompt;
        std::string default_model = "gpt-4o";
        int max_output_tokens = 4096;
        bool enable_compaction = true;
        bool enable_cache = true;
    };

    AgentLoop(
        Config config,
        std::shared_ptr<LlmProvider> llm,
        std::shared_ptr<ToolRegistry> tools,
        std::shared_ptr<MemoryStore> memory,
        std::shared_ptr<ContextAssembler> context,
        std::shared_ptr<Compactor> compactor
    );

    // Load history from persistent storage (journal restore).
    // Called once after creation, before first run().
    void restore_history(std::vector<Message> history);

    // Process a user message. Appends user msg to session_history_,
    // then enters the ReAct loop. Returns final response.
    std::future<AgentResponse> run(
        const std::string& user_message,
        RunControl& control);

    // Resume the ReAct loop without appending a new user message.
    // Used after approval restart where history is already set up.
    std::future<AgentResponse> resume(RunControl& control);

    TurnState current_state() const { return state_; }
    std::shared_ptr<ToolRegistry> tools() { return tools_; }
    const std::vector<Message>& session_history() const { return session_history_; }

private:
    Config config_;
    TurnState state_ = TurnState::Idle;
    std::shared_ptr<LlmProvider> llm_;
    std::shared_ptr<ToolRegistry> tools_;
    std::shared_ptr<MemoryStore> memory_;
    std::shared_ptr<ContextAssembler> context_;
    std::shared_ptr<Compactor> compactor_;
    std::shared_ptr<TokenCounter> counter_;
    std::shared_ptr<ToolResultCompactor> tool_result_compactor_;

    std::vector<Message> session_history_;
    std::map<std::string, int> tool_failure_streak_;
    static constexpr int kCircuitBreakerThreshold = 3;

    void transition_to(TurnState next, RunControl& control);
    std::vector<Message> build_context();
    std::vector<ToolResult> handle_tool_calls(
        const std::vector<ToolCall>& calls,
        RunControl& control
    );
    void maybe_compact(RunControl& control);

    // Internal ReAct loop. Assumes the latest user message is already
    // in session_history_. Called by both run() and resume().
    AgentResponse run_loop(RunControl& control);
};

} // namespace merak
