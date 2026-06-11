#include <merak/worldbuilding/pipeline_manager.hpp>
#include <merak/worldbuilding/pipeline.hpp>
#include <merak/worldbuilding/pipeline_validation.hpp>
#include <pqxx/pqxx>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <chrono>
#include <sstream>
#include <atomic>
#include <iomanip>
#include <set>
#include <stdexcept>

namespace merak::worldbuilding {

namespace {
    std::string current_iso_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::gmtime(&t), "%Y-%m-%dT%H:%M:%SZ");
        return oss.str();
    }

    std::string generate_uuid() {
        static std::atomic<unsigned long long> n = 0;
        auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        std::ostringstream oss;
        oss << std::hex << ticks << "-" << ++n;
        return oss.str();
    }

    nlohmann::json summary_to_json(const ConditionEvalSummary& summary,
                                    const PipelineState& state) {
        nlohmann::json j;
        j["phase"] = to_string(state.current_phase);
        j["all_met"] = summary.all_met;
        nlohmann::json conditions = nlohmann::json::array();
        for (auto& r : summary.results) {
            nlohmann::json cj;
            cj["name"] = r.message;
            cj["met"] = r.met;
            if (r.current) cj["current"] = *r.current;
            if (r.target) cj["target"] = *r.target;
            conditions.push_back(cj);
        }
        j["conditions"] = conditions;
        return j;
    }
}

const std::set<std::string> PipelineManager::RELEVANT_EVENTS = {
    "agent_created", "agent_card_updated", "relation_updated",
    "scene_created", "scene_ended", "chapter_created",
    "diary_written", "foreshadow_planted", "foreshadow_updated",
    "secret_created", "location_created", "knowledge_added",
    "world_time_advanced", "memory_summary_created"
};

PipelineManager::PipelineManager(Dependencies deps) : deps_(std::move(deps)) {
    if (!deps_.condition_evaluator) {
        throw std::invalid_argument("PipelineManager: condition_evaluator is required");
    }
    if (!deps_.pg_connection_factory) {
        throw std::invalid_argument("PipelineManager: pg_connection_factory is required");
    }
    start_time_ = std::chrono::steady_clock::now();
}
PipelineManager::~PipelineManager() = default;

std::string PipelineManager::advance_result_to_string(AdvanceResult r) {
    switch (r) {
        case AdvanceResult::SUCCESS: return "success";
        case AdvanceResult::INVALID_TRANSITION: return "invalid_transition";
        case AdvanceResult::CONDITIONS_NOT_MET: return "conditions_not_met";
        case AdvanceResult::ALREADY_AT_PHASE: return "already_at_phase";
        case AdvanceResult::NO_ACTIVE_STATE: return "no_active_state";
    }
    return "unknown";
}

// ─── Initialization ───
void PipelineManager::initialize() {
    ensure_tables();
    load_workflow_defs();
    prune_old_history();

    // Restore all active PipelineState from PostgreSQL to memory cache
    try {
        auto conn = deps_.pg_connection_factory();
        pqxx::work txn(*conn);
        auto result = txn.exec("SELECT world_id, state_json FROM pipeline_states");
        for (auto row : result) {
            auto world_id = row["world_id"].as<std::string>();
            auto state = nlohmann::json::parse(row["state_json"].as<std::string>())
                             .get<PipelineState>();
            std::unique_lock lock(world_mutex_);
            worlds_[world_id].state = std::move(state);
        }
        spdlog::info("PipelineManager: restored {} active pipeline states", worlds_.size());
    } catch (const std::exception& e) {
        spdlog::warn("PipelineManager: failed to restore states: {}", e.what());
    }
}

// ─── Workflow definitions ───
void PipelineManager::load_workflow_defs() {
    workflow_defs_.clear();
    if (!std::filesystem::exists(deps_.pipeline_config_dir)) {
        spdlog::warn("PipelineManager: config dir not found: {}", deps_.pipeline_config_dir.string());
        return;
    }
    for (auto& entry : std::filesystem::directory_iterator(deps_.pipeline_config_dir)) {
        if (entry.path().extension() != ".json") continue;
        try {
            std::ifstream f(entry.path());
            nlohmann::json j = nlohmann::json::parse(f);
            auto def = j.get<PipelineWorkflowDef>();
            auto validation_errors = validate_workflow_def(def, entry.path().string());
            bool has_errors = false;
            for (auto& ve : validation_errors) {
                if (ve.severity == PipelineValidationError::ERROR) {
                    spdlog::error("PipelineManager: validation error in {}: [{}] {}",
                                  ve.file_path, ve.field, ve.message);
                    has_errors = true;
                } else {
                    spdlog::warn("PipelineManager: validation warning in {}: [{}] {}",
                                 ve.file_path, ve.field, ve.message);
                }
            }
            if (has_errors) {
                spdlog::warn("PipelineManager: skipping invalid workflow '{}'", def.name);
                continue;
            }
            spdlog::info("PipelineManager: loaded workflow '{}' (v{}, {} phases)",
                         def.name, def.version, def.phases.size());
            workflow_defs_[def.name] = std::move(def);
        } catch (const std::exception& e) {
            spdlog::warn("PipelineManager: failed to load {}: {}", entry.path().string(), e.what());
        }
    }
}

const PipelineWorkflowDef* PipelineManager::get_workflow(const std::string& name) const {
    auto it = workflow_defs_.find(name);
    return it != workflow_defs_.end() ? &it->second : nullptr;
}

std::vector<std::string> PipelineManager::list_workflows() const {
    std::vector<std::string> names;
    for (auto& [name, _] : workflow_defs_) names.push_back(name);
    return names;
}

void PipelineManager::activate_workflow(const std::string& world_id,
                                         const std::string& workflow_name) {
    if (!workflow_defs_.count(workflow_name)) {
        spdlog::warn("PipelineManager: workflow '{}' not found", workflow_name);
        return;
    }
    {
        std::unique_lock lock(world_mutex_);
        worlds_[world_id].workflow_name = workflow_name;
    }
    init_state_for_world(world_id);
}

// ─── PipelineState CRUD ───
std::optional<PipelineState> PipelineManager::get_state(const std::string& world_id) const {
    std::shared_lock lock(world_mutex_);
    auto it = worlds_.find(world_id);
    if (it != worlds_.end()) return it->second.state;
    return std::nullopt;
}

void PipelineManager::init_state_for_world(const std::string& world_id) {
    std::string workflow_name;
    {
        std::shared_lock lock(world_mutex_);
        auto it = worlds_.find(world_id);
        workflow_name = (it != worlds_.end() && !it->second.workflow_name.empty())
                            ? it->second.workflow_name
                            : "default_creative_pipeline";
    }

    const auto* wf = get_workflow(workflow_name);
    if (!wf) {
        spdlog::warn("PipelineManager: workflow '{}' not found", workflow_name);
        return;
    }

    const auto* initial = wf->initial_phase();
    if (!initial) return;

    PipelineState state;
    state.world_id = world_id;
    state.current_phase = creative_phase_from_string(initial->key)
                              .value_or(CreativePhase::Worldbuilding);
    state.last_updated = current_iso_timestamp();
    state.active_workflow = wf->name;

    {
        std::unique_lock lock(world_mutex_);
        worlds_[world_id] = WorldEntry{state, wf->name};
    }

    persist_state(state);

    if (deps_.event_emitter) {
        AdvanceRequest dummy_req{world_id, state.current_phase, "init", "system", false, false};
        emit_phase_changed(state, initial, dummy_req);
    }

    spdlog::info("PipelineManager: initialized state for world {} at phase {}",
                 world_id, to_string(state.current_phase));
}

void PipelineManager::save_state(const PipelineState& state) {
    {
        std::unique_lock lock(world_mutex_);
        worlds_[state.world_id].state = state;
    }
    persist_state(state);
}

// ─── State persistence ───
void PipelineManager::persist_state(const PipelineState& state) {
    try {
        auto conn = deps_.pg_connection_factory();
        pqxx::work txn(*conn);
        nlohmann::json j = state;
        txn.exec_params0(R"(
            INSERT INTO pipeline_states (world_id, active_workflow, state_json)
            VALUES ($1, $2, $3)
            ON CONFLICT (world_id) DO UPDATE SET
                active_workflow = EXCLUDED.active_workflow,
                state_json = EXCLUDED.state_json,
                updated_at = NOW()
        )", state.world_id, state.active_workflow, j.dump());
        txn.commit();
    } catch (const std::exception& e) {
        spdlog::error("PipelineManager: failed to persist state for world {}: {}",
                      state.world_id, e.what());
    }
}

void PipelineManager::load_state_from_db(const std::string& world_id) {
    try {
        auto conn = deps_.pg_connection_factory();
        pqxx::work txn(*conn);
        auto row = txn.exec_params1(
            "SELECT state_json FROM pipeline_states WHERE world_id = $1", world_id);
        auto state = nlohmann::json::parse(row["state_json"].as<std::string>())
                         .get<PipelineState>();
        std::unique_lock lock(world_mutex_);
        worlds_[world_id].state = std::move(state);
    } catch (const pqxx::unexpected_rows&) {
        // No state for this world — that's fine
    }
}

void PipelineManager::ensure_tables() {
    try {
        auto conn = deps_.pg_connection_factory();
        pqxx::work txn(*conn);

        txn.exec(R"(
            CREATE TABLE IF NOT EXISTS pipeline_states (
                id              SERIAL PRIMARY KEY,
                world_id        VARCHAR(64) NOT NULL UNIQUE,
                active_workflow VARCHAR(128) NOT NULL DEFAULT 'default_creative_pipeline',
                state_json      JSONB NOT NULL,
                auto_advance    BOOLEAN NOT NULL DEFAULT true,
                require_confirm BOOLEAN NOT NULL DEFAULT false,
                user_confirm    BOOLEAN NOT NULL DEFAULT false,
                created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
                updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
            )
        )");

        txn.exec(R"(
            CREATE TABLE IF NOT EXISTS pipeline_history (
                id              VARCHAR(64) PRIMARY KEY,
                world_id        VARCHAR(64) NOT NULL,
                from_phase      VARCHAR(32) NOT NULL,
                to_phase        VARCHAR(32) NOT NULL,
                trigger_type    VARCHAR(32) NOT NULL DEFAULT 'auto',
                triggered_by    VARCHAR(128),
                conditions_json JSONB,
                created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
            )
        )");

        txn.exec(R"(
            CREATE INDEX IF NOT EXISTS idx_pipeline_history_created_at
                ON pipeline_history(created_at)
        )");
        txn.exec(R"(
            CREATE INDEX IF NOT EXISTS idx_pipeline_history_world_id
                ON pipeline_history(world_id)
        )");

        // Trigger for updated_at
        txn.exec(R"(
            CREATE OR REPLACE FUNCTION update_pipeline_updated_at()
            RETURNS TRIGGER AS $$
            BEGIN
                NEW.updated_at = NOW();
                RETURN NEW;
            END;
            $$ LANGUAGE plpgsql
        )");
        txn.exec(R"(
            DO $$
            BEGIN
                IF NOT EXISTS (
                    SELECT 1 FROM pg_trigger WHERE tgname = 'trigger_pipeline_updated_at'
                ) THEN
                    CREATE TRIGGER trigger_pipeline_updated_at
                        BEFORE UPDATE ON pipeline_states
                        FOR EACH ROW EXECUTE FUNCTION update_pipeline_updated_at();
                END IF;
            END;
            $$
        )");

        txn.commit();
        spdlog::info("PipelineManager: tables ensured");
    } catch (const std::exception& e) {
        spdlog::error("PipelineManager: failed to ensure tables: {}", e.what());
    }
}

// ─── Phase Advancement ───
PipelineManager::AdvanceResult PipelineManager::advance_phase(const AdvanceRequest& req) {
    // DepthGuard: prevent infinite recursion from action-triggered advances
    int depth = advance_depth_.fetch_add(1);
    if (depth >= MAX_ADVANCE_DEPTH) {
        advance_depth_.fetch_sub(1);
        spdlog::error("PipelineManager: advance_phase depth {} exceeds MAX_ADVANCE_DEPTH", depth);
        return AdvanceResult::INVALID_TRANSITION;
    }
    struct DepthGuard { std::atomic<int>& d; ~DepthGuard() { d.fetch_sub(1); } };
    DepthGuard guard{advance_depth_};

    PipelineState state;
    std::string wf_name;
    {
        std::shared_lock lock(world_mutex_);
        auto it = worlds_.find(req.world_id);
        if (it == worlds_.end()) return AdvanceResult::NO_ACTIVE_STATE;
        state = it->second.state;
        wf_name = it->second.workflow_name;
    }
    const auto* wf = get_workflow(wf_name);
    if (!wf) return AdvanceResult::NO_ACTIVE_STATE;

    // Determine target phase
    CreativePhase target;
    if (req.target_phase) {
        target = *req.target_phase;
    } else {
        auto allowed = allowed_next_phases(state.current_phase);
        if (allowed.empty()) return AdvanceResult::INVALID_TRANSITION;
        target = allowed[0]; // Default: first allowed phase
    }

    if (target == state.current_phase)
        return AdvanceResult::ALREADY_AT_PHASE;

    const auto* target_def = wf->get_phase(target);
    if (!target_def) return AdvanceResult::INVALID_TRANSITION;

    const auto* current_def = wf->get_phase(state.current_phase);

    // Validate transition
    if (!is_transition_allowed(state.current_phase, target, current_def))
        return AdvanceResult::INVALID_TRANSITION;

    // For forward movement, check advance_when conditions
    bool is_forward = static_cast<int>(target) > static_cast<int>(state.current_phase);
    ConditionEvalSummary conditions;
    if (is_forward && current_def && current_def->advance_when && !req.force) {
        conditions = evaluate_phase_conditions(state);
        if (!conditions.all_met)
            return AdvanceResult::CONDITIONS_NOT_MET;
    }

    // Execute on_complete actions of the current phase (transition decision hook).
    // Must fire after conditions verified met, before exit→transition→enter.
    // If on_complete triggers a goto_phase action, it handles the transition
    // and we must not proceed with the default transition below.
    if (is_forward && current_def && !current_def->on_complete.empty() && !req.force) {
        auto phase_before = state.current_phase;
        execute_actions(current_def->on_complete, state);
        // Reload: goto_phase calls advance_phase() which updates worlds_[id].state
        {
            std::shared_lock lock(world_mutex_);
            auto it = worlds_.find(req.world_id);
            if (it != worlds_.end() && it->second.state.current_phase != phase_before) {
                return AdvanceResult::SUCCESS;
            }
        }
    }

    auto old_phase = state.current_phase;

    // Execute exit actions of current phase
    if (current_def) execute_actions(current_def->on_exit, state);

    // Update state
    state.current_phase = target;
    state.last_updated = current_iso_timestamp();
    // Clear scene-level refs when moving to pre-scene phases
    if (target == CreativePhase::Worldbuilding ||
        target == CreativePhase::CharacterCreation ||
        target == CreativePhase::PlotArchitecture) {
        state.active_scene_id = std::nullopt;
    }

    // Persist
    {
        std::unique_lock lock(world_mutex_);
        worlds_[req.world_id].state = state;
    }
    persist_state(state);

    // Record transition history
    PhaseTransitionRecord record{
        generate_uuid(), req.world_id, old_phase, target,
        req.trigger, req.triggered_by, conditions, current_iso_timestamp()
    };
    record_transition(record);

    // Execute enter actions of target phase
    execute_actions(target_def->on_enter, state);

    // Emit SSE
    if (!req.skip_event) {
        emit_phase_changed(state, target_def, req);
    }

    spdlog::info("PipelineManager: world {} phase {} → {} (trigger: {})",
                 req.world_id, to_string(old_phase), to_string(target), req.trigger);

    return AdvanceResult::SUCCESS;
}

bool PipelineManager::is_transition_allowed(CreativePhase from, CreativePhase to,
                                             const PhaseDefinition* phase_def) const {
    // Check forward transition via allowed_next_phases()
    auto allowed = allowed_next_phases(from);
    if (std::find(allowed.begin(), allowed.end(), to) != allowed.end())
        return true;

    // For retreat/backward, check allowed_retreat in current phase definition
    if (phase_def) {
        auto target_str = to_string(to);
        if (std::find(phase_def->allowed_retreat.begin(),
                      phase_def->allowed_retreat.end(), target_str) !=
            phase_def->allowed_retreat.end()) {
            return true;
        }
    }
    return false;
}

// ─── Event Listener ───
void PipelineManager::on_world_event(const std::string& world_id,
                                      const std::string& event_type,
                                      const nlohmann::json& payload) {
    // 1. Filter irrelevant events
    if (!RELEVANT_EVENTS.count(event_type)) return;

    // 2. Debounce check
    auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(debounce_mutex_);
        auto it = last_eval_time_.find(world_id);
        if (it != last_eval_time_.end() && (now - it->second) < DEBOUNCE_WINDOW) {
            return; // Skip: within 2-second window
        }
        last_eval_time_[world_id] = now;
    }

    // 3. Get current state
    auto state_opt = get_state(world_id);
    if (!state_opt) return;

    std::string wf_name;
    {
        std::shared_lock lock(world_mutex_);
        auto it = worlds_.find(world_id);
        if (it == worlds_.end()) return;
        wf_name = it->second.workflow_name;
    }
    const auto* wf = get_workflow(wf_name);
    if (!wf || !wf->auto_advance) return; // Non-auto-advance mode, skip

    // 4. Evaluate conditions for current phase
    auto summary = evaluate_phase_conditions(*state_opt);

    // 5. Emit condition progress event (even if not all met, for frontend updates)
    if (deps_.event_emitter) {
        RuntimeEvent progress_event;
        progress_event.type = "pipeline_condition_progress";
        progress_event.payload = summary_to_json(summary, *state_opt);
        deps_.event_emitter(progress_event);
    }

    // 6. If all conditions met, auto-advance
    if (summary.all_met) {
        if (wf->require_confirmation) {
            // Require user confirmation mode
            if (deps_.event_emitter) {
                auto next_phases = allowed_next_phases(state_opt->current_phase);
                RuntimeEvent confirm_event;
                confirm_event.type = "pipeline_condition_met";
                confirm_event.payload = {
                    {"world_id", world_id},
                    {"phase", to_string(state_opt->current_phase)},
                    {"next_phase", next_phases.empty() ? "" : to_string(next_phases[0])},
                    {"conditions", summary_to_json(summary, *state_opt)["conditions"]}
                };
                deps_.event_emitter(confirm_event);
            }
        } else {
            // Auto-advance mode — check auto_loop first
            const auto* current_def = wf->get_phase(state_opt->current_phase);
            if (current_def && current_def->auto_loop) {
                bool should_continue = evaluate_loop_condition(
                    *current_def->auto_loop, *state_opt);
                if (should_continue) return; // Stay in current phase, continue looping
            }
            AdvanceRequest auto_req{world_id, std::nullopt, "auto", event_type, false, false};
            auto result = advance_phase(auto_req);
            if (result != AdvanceResult::SUCCESS && result != AdvanceResult::NO_ACTIVE_STATE) {
                handle_advance_failure(world_id, *state_opt, result);
            }
        }
    }
}

// ─── Transition History ───
void PipelineManager::record_transition(const PhaseTransitionRecord& record) {
    try {
        auto conn = deps_.pg_connection_factory();
        pqxx::work txn(*conn);
        nlohmann::json conditions_json;
        to_json(conditions_json, record.conditions_at_transition);
        txn.exec_params0(R"(
            INSERT INTO pipeline_history (id, world_id, from_phase, to_phase, trigger_type,
                                           triggered_by, conditions_json)
            VALUES ($1, $2, $3, $4, $5, $6, $7)
        )", record.id, record.world_id, to_string(record.from_phase), to_string(record.to_phase),
           record.trigger, record.triggered_by.value_or(""), conditions_json.dump());
        txn.commit();
    } catch (const std::exception& e) {
        spdlog::warn("PipelineManager: failed to record transition: {}", e.what());
    }
}

std::vector<PhaseTransitionRecord> PipelineManager::load_recent_history(
    const std::string& world_id, int limit) const {
    std::vector<PhaseTransitionRecord> records;
    try {
        auto conn = deps_.pg_connection_factory();
        pqxx::work txn(*conn);
        auto result = txn.exec_params(
            "SELECT id, world_id, from_phase, to_phase, trigger_type, triggered_by, "
            "conditions_json, created_at "
            "FROM pipeline_history WHERE world_id = $1 ORDER BY created_at DESC LIMIT $2",
            world_id, limit);
        for (auto row : result) {
            PhaseTransitionRecord rec;
            rec.id = row["id"].as<std::string>();
            rec.world_id = row["world_id"].as<std::string>();
            rec.from_phase = creative_phase_from_string(row["from_phase"].as<std::string>())
                                 .value_or(CreativePhase::Worldbuilding);
            rec.to_phase = creative_phase_from_string(row["to_phase"].as<std::string>())
                               .value_or(CreativePhase::Worldbuilding);
            rec.trigger = row["trigger_type"].as<std::string>();
            auto tb = row["triggered_by"].as<std::string>();
            if (!tb.empty()) rec.triggered_by = tb;
            rec.timestamp = row["created_at"].as<std::string>();
            records.push_back(std::move(rec));
        }
    } catch (const std::exception& e) {
        spdlog::warn("PipelineManager: failed to load history: {}", e.what());
    }
    return records;
}

// ─── Condition Evaluation ───
ConditionEvalSummary PipelineManager::evaluate_phase_conditions(
    const PipelineState& state) const {
    std::string wf_name;
    {
        std::shared_lock lock(world_mutex_);
        auto it = worlds_.find(state.world_id);
        if (it == worlds_.end()) return {};
        wf_name = it->second.workflow_name;
    }
    const auto* wf = get_workflow(wf_name);
    if (!wf) return {};

    const auto* phase_def = wf->get_phase(state.current_phase);
    if (!phase_def || !phase_def->advance_when) return {};

    auto conn = deps_.pg_connection_factory();
    return deps_.condition_evaluator
        ->evaluate_group(*phase_def->advance_when, state, *conn,
                        to_string(state.current_phase));
}

ConditionResult PipelineManager::evaluate_single_condition(
    const ConditionDef& cond, const PipelineState& state) const {
    auto conn = deps_.pg_connection_factory();
    return deps_.condition_evaluator->evaluate(cond, state, *conn);
}

// ─── Context Injection ───
std::string PipelineManager::get_phase_context(const std::string& world_id) const {
    auto state_opt = get_state(world_id);
    if (!state_opt) return "";
    return generate_phase_context(*state_opt);
}

std::vector<std::string> PipelineManager::get_allowed_tools(const std::string& world_id) const {
    auto state_opt = get_state(world_id);
    if (!state_opt) return {};

    std::string wf_name;
    {
        std::shared_lock lock(world_mutex_);
        auto it = worlds_.find(world_id);
        if (it == worlds_.end()) return {};
        wf_name = it->second.workflow_name;
    }
    const auto* wf = get_workflow(wf_name);
    if (!wf) return {};

    const auto* phase_def = wf->get_phase(state_opt->current_phase);
    if (!phase_def) return {};

    return phase_def->allowed_tools;
}

nlohmann::json PipelineManager::get_phase_injection_config(const std::string& world_id) const {
    auto state_opt = get_state(world_id);
    if (!state_opt) return nlohmann::json::object();

    std::string wf_name;
    {
        std::shared_lock lock(world_mutex_);
        auto it = worlds_.find(world_id);
        if (it == worlds_.end()) return nlohmann::json::object();
        wf_name = it->second.workflow_name;
    }
    const auto* wf = get_workflow(wf_name);
    if (!wf) return nlohmann::json::object();

    const auto* phase_def = wf->get_phase(state_opt->current_phase);
    if (!phase_def) return nlohmann::json::object();

    nlohmann::json config;
    config["inject"] = phase_def->context.inject;
    config["extra"] = phase_def->context.extra;
    return config;
}

// ─── Action Execution ───
void PipelineManager::execute_actions(const std::vector<ActionDef>& actions,
                                       const PipelineState& state) {
    for (auto& action : actions) {
        if (action.type == "log") {
            auto level = action.params.value("level", "info");
            auto msg = action.params.value("message", "");
            if (level == "info") spdlog::info("PipelineAction: {}", msg);
            else if (level == "warn") spdlog::warn("PipelineAction: {}", msg);
            else spdlog::info("PipelineAction: {}", msg);
        } else if (action.type == "emit_sse") {
            if (deps_.event_emitter) {
                RuntimeEvent event;
                event.type = action.params.value("event", "pipeline_action");
                event.payload = action.params.value("params", nlohmann::json::object());
                deps_.event_emitter(event);
            }
        } else if (action.type == "goto_phase") {
            auto target_str = action.params.value("target", "");
            auto target = creative_phase_from_string(target_str);
            if (target) {
                AdvanceRequest auto_req{state.world_id, *target, "workflow_action",
                                        std::nullopt, false, false};
                advance_phase(auto_req);
            }
        } else if (action.type == "conditional") {
            // Construct ConditionDef from params — support two modes:
            //   "condition": full JSON object → from_json deserialization
            //   "condition_type": simple string (backward compatible)
            ConditionDef cond;
            if (action.params.contains("condition")) {
                from_json(action.params["condition"], cond);
            } else if (action.params.contains("condition_type")) {
                cond.type = action.params["condition_type"].get<std::string>();
            } else {
                spdlog::warn("PipelineAction: conditional action missing condition or condition_type");
                continue;
            }
            if (cond.message.empty()) cond.message = "conditional action check";

            auto conn = deps_.pg_connection_factory();
            auto result = deps_.condition_evaluator->evaluate(cond, state, *conn);

            if (result.met && action.params.contains("then")) {
                ActionDef then_action;
                from_json(action.params["then"], then_action);
                execute_actions({then_action}, state);
            } else if (!result.met && action.params.contains("else")) {
                ActionDef else_action;
                from_json(action.params["else"], else_action);
                execute_actions({else_action}, state);
            }
        }
    }
}

// ─── SSE Emission ───
void PipelineManager::emit_phase_changed(const PipelineState& state,
                                          const PhaseDefinition* phase_def,
                                          const AdvanceRequest& req) {
    if (!deps_.event_emitter) return;

    nlohmann::json payload;
    payload["world_id"] = state.world_id;
    payload["phase"] = to_string(state.current_phase);
    if (phase_def) {
        payload["label"] = phase_def->label;
        payload["allowed_tools"] = phase_def->allowed_tools;
        payload["allowed_retreat"] = phase_def->allowed_retreat;

        // Include conditions for the new phase
        auto summary = evaluate_phase_conditions(state);
        payload["conditions"] = summary_to_json(summary, state)["conditions"];
        payload["all_conditions_met"] = summary.all_met;

        // Next allowed phases
        auto next_phases = allowed_next_phases(state.current_phase);
        nlohmann::json next_arr = nlohmann::json::array();
        for (auto& np : next_phases) next_arr.push_back(to_string(np));
        payload["next_allowed"] = next_arr;
    }

    // Transition metadata
    payload["trigger"] = req.trigger;
    if (req.triggered_by) payload["triggered_by"] = *req.triggered_by;

    RuntimeEvent event;
    event.type = "pipeline_phase_changed";
    event.payload = payload;
    deps_.event_emitter(event);
}

// ─── View Data (for frontend) ───
PipelineManager::PipelineViewData PipelineManager::get_view_data(
    const std::string& world_id) const {
    PipelineViewData data;
    auto state_opt = get_state(world_id);
    if (!state_opt) return data;

    data.state = *state_opt;
    data.active_workflow_name = data.state.active_workflow;
    data.current_conditions = evaluate_phase_conditions(data.state);
    data.recent_history = load_recent_history(world_id, 10);
    return data;
}

// ─── Checkpoint Serialization ───
std::string PipelineManager::snapshot_to_json(const std::string& world_id) const {
    auto state_opt = get_state(world_id);
    if (!state_opt) return "{}";

    nlohmann::json snapshot;
    to_json(snapshot, *state_opt);
    snapshot["_snapshot_at"] = current_iso_timestamp();

    {
        std::shared_lock lock(world_mutex_);
        auto it = worlds_.find(world_id);
        if (it != worlds_.end()) {
            snapshot["_active_workflow"] = it->second.workflow_name;
        }
    }

    return snapshot.dump();
}

void PipelineManager::restore_from_snapshot(const std::string& world_id,
                                              const std::string& json) {
    try {
        auto j = nlohmann::json::parse(json);
        auto state = j.get<PipelineState>();
        {
            std::unique_lock lock(world_mutex_);
            worlds_[world_id] = WorldEntry{state, j.value("_active_workflow", "")};
        }
        persist_state(state);
        spdlog::info("PipelineManager: restored state from snapshot for world {}", world_id);
    } catch (const std::exception& e) {
        spdlog::error("PipelineManager: failed to restore from snapshot: {}", e.what());
    }
}

// ─── Loop Condition Evaluation ───
bool PipelineManager::evaluate_loop_condition(const AutoLoopDef& loop,
                                               const PipelineState& state) const {
    const std::string& expr = loop.continue_while;
    if (expr.empty()) return false;

    // Parse expression: "field op value"
    // Supported fields: scene_count, total_scenes_target, chapter_count,
    //                   total_chapters_target, cycle_count
    // Supported ops: <, >, <=, >=, ==, !=
    // Value can be a field name or an integer literal

    auto resolve = [&](const std::string& token) -> std::optional<int> {
        if (token == "scene_count") return state.scene_count_in_chapter;
        if (token == "total_scenes_target") return state.total_scenes_target;
        if (token == "chapter_count") return state.chapter_count;
        if (token == "total_chapters_target") return state.total_chapters_target;
        if (token == "cycle_count") return state.cycle_count;
        // Try integer literal
        try {
            return std::stoi(token);
        } catch (...) {
            return std::nullopt;
        }
    };

    // Split by whitespace into tokens
    std::vector<std::string> tokens;
    std::istringstream iss(expr);
    std::string tok;
    while (iss >> tok) tokens.push_back(tok);

    if (tokens.size() < 3) return false;

    auto lhs = resolve(tokens[0]);
    std::string op = tokens[1];

    // Handle potential two-char operators (>=, <=, !=, ==)
    std::string rhs_str;
    if (tokens.size() >= 3) {
        rhs_str = tokens[2];
    } else {
        return false;
    }

    auto rhs = resolve(rhs_str);
    if (!lhs || !rhs) return false;

    if (op == "<")  return *lhs < *rhs;
    if (op == ">")  return *lhs > *rhs;
    if (op == "<=") return *lhs <= *rhs;
    if (op == ">=") return *lhs >= *rhs;
    if (op == "==") return *lhs == *rhs;
    if (op == "!=") return *lhs != *rhs;

    return false;
}

// ─── Advance Failure Handling ───
void PipelineManager::handle_advance_failure(const std::string& world_id,
                                              const PipelineState& state,
                                              AdvanceResult result) {
    // Store last_error in state.extra
    auto mutable_state = state;
    mutable_state.extra["last_error"] = {
        {"result", advance_result_to_string(result)},
        {"timestamp", current_iso_timestamp()}
    };

    // Update cache and persist
    save_state(mutable_state);

    // Emit SSE event
    if (deps_.event_emitter) {
        RuntimeEvent event;
        event.type = "pipeline_advance_failed";
        event.payload = {
            {"world_id", world_id},
            {"phase", to_string(state.current_phase)},
            {"result", advance_result_to_string(result)}
        };
        deps_.event_emitter(event);
    }
}

void PipelineManager::clear_last_error(const std::string& world_id) {
    std::unique_lock lock(world_mutex_);
    auto it = worlds_.find(world_id);
    if (it == worlds_.end()) return;
    if (it->second.state.extra.contains("last_error")) {
        it->second.state.extra.erase("last_error");
        persist_state(it->second.state);
    }
}

// ─── Metrics ───
PipelineManager::PipelineMetrics PipelineManager::get_metrics() const {
    PipelineMetrics m{};
    m.start_time = start_time_;

    {
        std::shared_lock lock(world_mutex_);
        m.active_states = worlds_.size();
    }

    if (deps_.condition_evaluator) {
        const auto& stats = deps_.condition_evaluator->stats();
        m.total_evaluations = stats.total_evaluations.load();
        m.total_failures = stats.total_failures.load();
        m.total_errors = stats.total_errors.load();
        m.last_eval_time = stats.last_eval_time;
    }

    // Count transitions from DB history table
    try {
        auto conn = deps_.pg_connection_factory();
        pqxx::work txn(*conn);
        auto row = txn.exec1("SELECT COUNT(*) FROM pipeline_history");
        m.total_transitions = row[0].as<size_t>();
    } catch (...) {
        m.total_transitions = 0;
    }

    return m;
}

// ─── History Maintenance ───
void PipelineManager::prune_old_history() {
    try {
        auto conn = deps_.pg_connection_factory();
        pqxx::work txn(*conn);
        txn.exec("DELETE FROM pipeline_history WHERE created_at < NOW() - INTERVAL '30 days'");
        txn.commit();
    } catch (const std::exception& e) {
        spdlog::warn("PipelineManager: failed to prune history: {}", e.what());
    }
}

} // namespace merak::worldbuilding
