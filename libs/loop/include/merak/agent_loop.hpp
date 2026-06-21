#pragma once
#include <merak/message.hpp>
#include <merak/turn_state.hpp>
#include <merak/config.hpp>
#include <merak/error.hpp>
#include <merak/llm_provider.hpp>
#include <merak/tool_registry.hpp>
#include <merak/memory_store.hpp>
#include <merak/context_pipeline.hpp>
#include <merak/token_counter.hpp>
#include <merak/compactor.hpp>
#include <merak/cache_aware_context.hpp>
#include <merak/stall_detector.hpp>
#include <merak/turn_guard.hpp>
#include <merak/turn_ingestor.hpp>
#include <merak/skills/skill_registry.hpp>
#include <merak/execution.hpp>
#include <chrono>
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
        int circuit_breaker_threshold = 3;
        bool enable_compaction = true;
        bool enable_cache = true;
        int tool_timeout_ms = 30000;
        struct ToolRateLimit {
            int max_calls_per_turn = 50;
            int max_calls_per_run = 500;
        };
        ToolRateLimit tool_rate_limit;
    };

    struct RunMetrics {
        int turns_completed = 0;
        int total_input_tokens = 0;
        int total_output_tokens = 0;
        int total_cache_read_tokens = 0;
        int total_cache_write_tokens = 0;
        int total_tool_calls = 0;
        int tool_errors = 0;
        int compactions_triggered = 0;
        int messages_compacted = 0;
        int circuit_breaker_trips = 0;
        int stall_force_stops = 0;
        int turn_guard_warnings = 0;
        int abandoned_tasks = 0;
        std::chrono::milliseconds total_llm_latency{0};
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

    // Store the runtime system prompt (used by build_context if non-empty).
    void set_system_prompt(const std::string& prompt);

    // Set a callback that provides narrative working memory context text.
    // When not set (default), working_memory_text returns empty string.
    void set_working_memory_provider(std::function<std::string()> provider);

    AgentLoop(const AgentLoop&) = delete;
    AgentLoop& operator=(const AgentLoop&) = delete;
    AgentLoop(AgentLoop&&) = delete;
    AgentLoop& operator=(AgentLoop&&) = delete;

    // Process a user message. Appends user msg to session_history_,
    // then enters the ReAct loop. Returns final response.
    AgentResponse run(
        const std::string& user_message,
        RunControl& control);

    // Resume the ReAct loop without appending a new user message.
    AgentResponse resume(RunControl& control);

    TurnState current_state() const { return state_; }
    const RunMetrics& metrics() const { return run_metrics_; }
    std::shared_ptr<ToolRegistry> tools() { return tools_; }
    const std::vector<Message>& session_history() const { return session_history_; }

    ContextPipeline& pipeline() { return *pipeline_; }

    void set_plan_mode_source(std::shared_ptr<std::atomic<bool>> source) { plan_mode_ = std::move(source); }
    void set_active_world_id(std::optional<std::string> world_id);
    void set_active_scene_id(std::optional<std::string> scene_id);
    void set_caller_agent_id(std::optional<std::string> agent_id);
    void set_worldbuilding_service(std::shared_ptr<worldbuilding::WorldbuildingService> worldbuilding);
    void set_skill_registry(std::shared_ptr<skills::SkillRegistry> skills);
    bool is_plan_mode() const { return plan_mode_ && plan_mode_->load(); }
    TurnIngestor& turn_ingestor() { return turn_ingestor_; }

private:
    Config config_;
    TurnState state_ = TurnState::Idle;
    std::shared_ptr<LlmProvider> llm_;
    std::shared_ptr<ToolRegistry> tools_;
    std::shared_ptr<MemoryStore> memory_;
    std::shared_ptr<Compactor> compactor_;
    std::shared_ptr<TokenCounter> token_counter_;

    std::unique_ptr<ContextPipeline> pipeline_;
    StallDetector stall_detector_;
    TurnGuard turn_guard_;
    TurnIngestor turn_ingestor_;

    std::vector<Message> session_history_;
    std::string system_prompt_;
    std::vector<Message> compaction_summaries_;
    std::shared_ptr<std::atomic<bool>> plan_mode_;
    std::function<std::string()> working_memory_provider_;
    std::shared_ptr<worldbuilding::WorldbuildingService> worldbuilding_;
    std::shared_ptr<skills::SkillRegistry> skills_;
    std::optional<std::string> active_world_id_;
    std::optional<std::string> active_scene_id_;
    std::optional<std::string> caller_agent_id_;
    std::map<std::string, int> tool_failure_streak_;

    int consecutive_read_only_rounds_ = 0;
    int consecutive_world_query_rounds_ = 0;
    int consecutive_content_avoidance_ = 0;
    int run_call_count_ = 0;
    std::vector<std::future<ToolResult>> abandoned_tasks_;
    static constexpr size_t kMaxAbandonedTasks = 32;
    int current_turn_ = 0;

    std::string last_user_query_;
    std::string last_compaction_text_;
    int turn_call_count_ = 0;

    ToolDomain restricted_domains_ = ToolDomain::General;
    RunMetrics run_metrics_;

    void transition_to(TurnState next, RunControl& control);
    std::vector<Message> build_context();
    std::vector<ToolResult> handle_tool_calls(
        const std::vector<ToolCall>& calls,
        RunControl& control
    );
    void maybe_compact(RunControl& control);
    void drain_abandoned_tasks();

    AgentResponse run_loop(RunControl& control);
};

} // namespace merak
