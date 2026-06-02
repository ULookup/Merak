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

    std::future<AgentResponse> run(
        const std::string& user_message,
        RunControl& control,
        std::vector<Message> initial_history = {},
        bool append_user_message = true);
    TurnState current_state() const { return state_; }
    std::shared_ptr<ToolRegistry> tools() { return tools_; }

private:
    Config config_;
    TurnState state_ = TurnState::Idle;
    std::shared_ptr<LlmProvider> llm_;
    std::shared_ptr<ToolRegistry> tools_;
    std::shared_ptr<MemoryStore> memory_;
    std::shared_ptr<ContextAssembler> context_;
    std::shared_ptr<Compactor> compactor_;
    std::shared_ptr<TokenCounter> counter_;

    std::vector<Message> session_history_;
    std::map<std::string, int> tool_failure_streak_;
    static constexpr int kCircuitBreakerThreshold = 3;

    void transition_to(TurnState next, RunControl& control);
    std::vector<Message> build_context(const std::string& user_message);
    std::vector<ToolResult> handle_tool_calls(
        const std::vector<ToolCall>& calls,
        RunControl& control
    );
    void maybe_compact(RunControl& control);
};

} // namespace merak
