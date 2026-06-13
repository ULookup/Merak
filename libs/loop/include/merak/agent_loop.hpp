#pragma once
#include <merak/message.hpp>
#include <merak/turn_state.hpp>
#include <merak/config.hpp>
#include <merak/error.hpp>
#include <merak/llm_provider.hpp>
#include <merak/tool_registry.hpp>
#include <merak/memory_store.hpp>
#include <merak/context_pipeline.hpp>
#include <merak/compactor.hpp>
#include <merak/cache_aware_context.hpp>
#include <merak/stall_detector.hpp>
#include <merak/turn_guard.hpp>
#include <merak/turn_ingestor.hpp>
#include <merak/skills/skill_registry.hpp>
#include <merak/execution.hpp>
#include <optional>
#include <atomic>
#include <functional>
#include <future>
#include <memory>

namespace merak::worldbuilding { class WorldbuildingService; }

namespace merak {

class AgentLoop {
public:
    struct Config {
        int max_turns = 25;
        std::string system_prompt;
        std::string default_model = "gpt-4o";
        int max_output_tokens = 4096;
        int max_retries = 3;
        int model_max_tokens = 128000;
        bool enable_compaction = true;
        bool enable_cache = true;
    };

    AgentLoop(
        Config config,
        std::shared_ptr<LlmProvider> llm,
        std::shared_ptr<ToolRegistry> tools,
        std::shared_ptr<MemoryStore> memory,
        std::shared_ptr<Compactor> compactor,
        std::shared_ptr<worldbuilding::WorldbuildingService> worldbuilding,
        std::shared_ptr<skills::SkillRegistry> skills
    );

    // Load history from persistent storage (journal restore).
    void restore_history(std::vector<Message> history);

    // Replace or insert system message at position 0.
    void set_system_prompt(const std::string& prompt);

    // Set a callback that provides narrative working memory context text.
    // When not set (default), working_memory_text returns empty string.
    void set_working_memory_provider(std::function<std::string()> provider);

    // Process a user message. Appends user msg to session_history_,
    // then enters the ReAct loop. Returns final response.
    std::future<AgentResponse> run(
        const std::string& user_message,
        RunControl& control);

    // Resume the ReAct loop without appending a new user message.
    std::future<AgentResponse> resume(RunControl& control);

    TurnState current_state() const { return state_; }
    std::shared_ptr<ToolRegistry> tools() { return tools_; }
    const std::vector<Message>& session_history() const { return session_history_; }

    ContextPipeline& pipeline() { return *pipeline_; }

    void set_plan_mode_source(std::shared_ptr<std::atomic<bool>> source) { plan_mode_ = std::move(source); }
    void set_active_world_id(std::optional<std::string> world_id);
    bool is_plan_mode() const { return plan_mode_ && plan_mode_->load(); }
    TurnIngestor& turn_ingestor() { return turn_ingestor_; }

private:
    Config config_;
    TurnState state_ = TurnState::Idle;
    std::shared_ptr<LlmProvider> llm_;
    std::shared_ptr<ToolRegistry> tools_;
    std::shared_ptr<MemoryStore> memory_;
    std::shared_ptr<Compactor> compactor_;

    std::unique_ptr<ContextPipeline> pipeline_;
    StallDetector stall_detector_;
    TurnGuard turn_guard_;
    TurnIngestor turn_ingestor_;

    std::vector<Message> session_history_;
    std::shared_ptr<std::atomic<bool>> plan_mode_;
    std::function<std::string()> working_memory_provider_;
    std::shared_ptr<worldbuilding::WorldbuildingService> worldbuilding_;
    std::shared_ptr<skills::SkillRegistry> skills_;
    std::optional<std::string> active_world_id_;
    std::map<std::string, int> tool_failure_streak_;
    static constexpr int kCircuitBreakerThreshold = 3;

    int consecutive_read_only_rounds_ = 0;
    int consecutive_world_query_rounds_ = 0;
    int consecutive_content_avoidance_ = 0;
    int current_turn_ = 0;

    std::vector<std::string> restricted_tools_;

    void transition_to(TurnState next, RunControl& control);
    std::vector<Message> build_context();
    std::vector<ToolResult> handle_tool_calls(
        const std::vector<ToolCall>& calls,
        RunControl& control
    );
    void maybe_compact(RunControl& control);

    AgentResponse run_loop(RunControl& control);
};

} // namespace merak
