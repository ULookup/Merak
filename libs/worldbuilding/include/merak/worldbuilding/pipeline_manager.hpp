#pragma once
#include <merak/worldbuilding/pipeline_workflow_def.hpp>
#include <merak/worldbuilding/condition_evaluator.hpp>
#include <merak/runtime_event.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

namespace pqxx { class connection; }

namespace merak::worldbuilding {

class PipelineManager {
public:
    // ─── Agent invocation result ───
    struct AgentInvocationResult {
        bool success = false;
        std::string output;
        std::string error;
    };

    // ─── Dependency injection ───
    struct Dependencies {
        std::function<std::shared_ptr<pqxx::connection>()> pg_connection_factory;
        std::function<void(const RuntimeEvent&)> event_emitter;
        std::function<AgentInvocationResult(const std::string& world_id,
                                            const std::string& agent_id,
                                            const std::string& task_description)> invoke_agent;
        std::filesystem::path pipeline_config_dir;
        std::filesystem::path worlds_base_dir;
        std::shared_ptr<ConditionEvaluator> condition_evaluator;
    };

    explicit PipelineManager(Dependencies deps);
    ~PipelineManager();

    // ─── Initialization ───
    void initialize(); // load workflow defs, create tables, restore active states

    // ─── Workflow definition management ───
    void load_workflow_defs(); // scan config/pipelines/*.json
    const PipelineWorkflowDef* get_workflow(const std::string& name) const;
    std::vector<std::string> list_workflows() const;
    bool activate_workflow(const std::string& world_id, const std::string& workflow_name);

    // ─── PipelineState CRUD ───
    std::optional<PipelineState> get_state(const std::string& world_id) const;
    void init_state_for_world(const std::string& world_id);
    void save_state(const PipelineState& state);

    // ─── Phase advancement ───
    enum class AdvanceResult {
        SUCCESS,
        INVALID_TRANSITION,
        CONDITIONS_NOT_MET,
        ALREADY_AT_PHASE,
        NO_ACTIVE_STATE
    };

    struct AdvanceRequest {
        std::string world_id;
        std::optional<CreativePhase> target_phase; // empty = auto-find next
        std::string trigger = "manual";
        std::optional<std::string> triggered_by;
        bool force = false;      // skip condition check
        bool skip_event = false; // suppress SSE emission (for restore)
    };

    AdvanceResult advance_phase(const AdvanceRequest& req);
    static std::string advance_result_to_string(AdvanceResult r);

    // ─── Loop condition evaluation ───
    bool evaluate_loop_condition(const AutoLoopDef& loop, const PipelineState& state) const;

    // ─── Error handling ───
    void handle_advance_failure(const std::string& world_id, const PipelineState& state, AdvanceResult result);
    void clear_last_error(const std::string& world_id);

    // ─── Metrics ───
    struct PipelineMetrics {
        size_t active_states;
        size_t total_transitions;
        uint64_t total_evaluations;
        uint64_t total_failures;
        uint64_t total_errors;
        std::chrono::steady_clock::time_point start_time;
        std::chrono::steady_clock::time_point last_eval_time;
    };
    PipelineMetrics get_metrics() const;

    // ─── History maintenance ───
    void prune_old_history();
    std::vector<PhaseTransitionRecord> load_recent_history(const std::string& world_id,
                                                            int limit = 10) const;

    // ─── Condition evaluation ───
    ConditionEvalSummary evaluate_phase_conditions(const PipelineState& state) const;
    ConditionResult evaluate_single_condition(const ConditionDef& cond,
                                              const PipelineState& state) const;

    // ─── Event listener (called by RuntimeService) ───
    void on_world_event(const std::string& world_id,
                        const std::string& event_type,
                        const nlohmann::json& payload);

    // ─── Context injection ───
    std::string get_phase_context(const std::string& world_id) const;
    std::vector<std::string> get_allowed_tools(const std::string& world_id) const;
    nlohmann::json get_phase_injection_config(const std::string& world_id) const;

    // ─── Frontend view data ───
    struct PipelineViewData {
        PipelineState state;
        std::string active_workflow_name;
        ConditionEvalSummary current_conditions;
        std::vector<PhaseTransitionRecord> recent_history; // last 10
    };
    PipelineViewData get_view_data(const std::string& world_id) const;

    // ─── Checkpoint serialization ───
    std::string snapshot_to_json(const std::string& world_id) const;
    void restore_from_snapshot(const std::string& world_id, const std::string& json);

private:
    Dependencies deps_;

    // Loaded workflow definitions: name → def
    std::map<std::string, PipelineWorkflowDef> workflow_defs_;

    // Unified world state (single lock, atomic read/write)
    struct WorldEntry {
        PipelineState state;
        std::string workflow_name;
    };

    mutable std::shared_mutex world_mutex_;
    std::map<std::string, WorldEntry> worlds_;

    // Debounce: prevent repeated condition evaluation within 2 seconds
    mutable std::mutex debounce_mutex_;
    std::map<std::string, std::chrono::steady_clock::time_point> last_eval_time_;
    static constexpr auto DEBOUNCE_WINDOW = std::chrono::milliseconds(2000);

    // Relevant event types that trigger condition re-evaluation
    static const std::set<std::string> RELEVANT_EVENTS;

    // Internal methods
    void ensure_tables();
    void load_state_from_db(const std::string& world_id);
    void persist_state(const PipelineState& state);
    void record_transition(const PhaseTransitionRecord& record);
    void execute_actions(const std::vector<ActionDef>& actions, const PipelineState& state);
    void emit_phase_changed(const PipelineState& state,
                            const PhaseDefinition* phase_def,
                            const AdvanceRequest& req);
    bool is_transition_allowed(CreativePhase from, CreativePhase to,
                               const PhaseDefinition* phase_def) const;

    std::atomic<int> advance_depth_{0};
    static constexpr int MAX_ADVANCE_DEPTH = 32;
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace merak::worldbuilding
