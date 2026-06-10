#pragma once
#include <merak/worldbuilding/pipeline.hpp>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace merak::worldbuilding {

// ─── Condition operator enum ───
enum class ConditionOp { EQ, NEQ, GT, GTE, LT, LTE, CONTAINS, EXISTS };

inline ConditionOp op_from_string(const std::string& s) {
    if (s == "==" || s == "eq") return ConditionOp::EQ;
    if (s == "!=" || s == "neq") return ConditionOp::NEQ;
    if (s == ">" || s == "gt") return ConditionOp::GT;
    if (s == ">=" || s == "gte") return ConditionOp::GTE;
    if (s == "<" || s == "lt") return ConditionOp::LT;
    if (s == "<=" || s == "lte") return ConditionOp::LTE;
    if (s == "contains") return ConditionOp::CONTAINS;
    if (s == "exists") return ConditionOp::EXISTS;
    return ConditionOp::EQ;
}

// ─── Single condition definition ───
struct ConditionDef {
    std::string type;                              // "entity_count" | "all_characters_have_cards" | ...
    std::string entity;                             // target entity: agent / location / chapter / scene / ...
    std::optional<std::string> kind_filter;         // optional kind filter: individual / god / ...
    ConditionOp op = ConditionOp::GTE;
    std::optional<int> target_int;                  // integer target value
    std::optional<std::string> target_str;          // string target or variable reference ($total_scenes_target)
    std::optional<std::vector<std::string>> checks; // check items for all_checks_passed
    std::string message;                            // user-visible description
};

// ─── Condition group ───
struct ConditionGroup {
    std::string operator_type = "and"; // "and" | "or"
    std::vector<ConditionDef> conditions;
};

// ─── Action definition ───
struct ActionDef {
    std::string type; // "emit_sse" | "update_checkpoint" | "log" | "validate" | "goto_phase" | "conditional"
    nlohmann::json params;
};

// ─── Phase context injection config ───
struct PhaseContextConfig {
    std::vector<std::string> inject; // ["phase_guidance","available_tools","world_summary",...]
    nlohmann::json extra;            // e.g. include_existing_characters: true
};

// ─── Auto-loop for per-scene iteration (scene_writing phase) ───
struct AutoLoopDef {
    std::string entity;       // "chapter"
    std::string target;       // "all_scenes_in_chapter"
    std::string continue_while; // "scene_count < total_scenes_target"
};

// ─── Single phase definition ───
struct PhaseDefinition {
    std::string key;                             // "worldbuilding"
    std::string label;                           // "世界观构建"
    bool initial = false;                        // is this the starting phase?

    PhaseContextConfig context;                  // context injection config
    std::vector<std::string> allowed_tools;      // tool whitelist for this phase
    std::optional<ConditionGroup> advance_when;  // advance conditions (empty = manual only)
    std::vector<std::string> allowed_retreat;    // phases that can retreat into this one

    std::vector<ActionDef> on_enter;             // actions on phase entry
    std::vector<ActionDef> on_exit;              // actions on phase exit
    std::vector<ActionDef> on_complete;          // actions after phase completion

    std::optional<AutoLoopDef> auto_loop;        // per-scene loop (scene_writing only)
};

// ─── Pipeline workflow definition ───
struct PipelineWorkflowDef {
    std::string name;                    // "default_creative_pipeline"
    std::string description;
    int version = 1;
    bool auto_advance = true;            // auto-advance when conditions met
    bool require_confirmation = false;   // require user confirmation (overrides auto_advance)
    std::vector<PhaseDefinition> phases;

    const PhaseDefinition* initial_phase() const {
        for (auto& p : phases) if (p.initial) return &p;
        return phases.empty() ? nullptr : &phases[0];
    }
    const PhaseDefinition* get_phase(const std::string& key) const {
        for (auto& p : phases) if (p.key == key) return &p;
        return nullptr;
    }
    const PhaseDefinition* get_phase(CreativePhase phase) const {
        return get_phase(to_string(phase));
    }
};

// ─── Condition evaluation result ───
struct ConditionResult {
    std::string message;
    bool met = false;
    std::optional<int> current;
    std::optional<int> target;
    nlohmann::json extra;
};

// ─── Condition evaluation summary ───
struct ConditionEvalSummary {
    std::string phase_key;
    bool all_met = false;
    std::vector<ConditionResult> results;
};

// ─── Phase transition record ───
struct PhaseTransitionRecord {
    std::string id;
    std::string world_id;
    CreativePhase from_phase;
    CreativePhase to_phase;
    std::string trigger;                    // "auto" | "manual" | "workflow_action"
    std::optional<std::string> triggered_by; // "user_click" | agent_id | event_type
    ConditionEvalSummary conditions_at_transition;
    std::string timestamp;
};

// ─── JSON deserialization declarations ───
void from_json(const nlohmann::json& j, ConditionDef& c);
void from_json(const nlohmann::json& j, ConditionGroup& g);
void from_json(const nlohmann::json& j, ActionDef& a);
void from_json(const nlohmann::json& j, PhaseContextConfig& c);
void from_json(const nlohmann::json& j, AutoLoopDef& l);
void from_json(const nlohmann::json& j, PhaseDefinition& p);
void from_json(const nlohmann::json& j, PipelineWorkflowDef& w);
void to_json(nlohmann::json& j, const ConditionResult& r);
void to_json(nlohmann::json& j, const ConditionEvalSummary& s);
void to_json(nlohmann::json& j, const PhaseTransitionRecord& r);
void from_json(const nlohmann::json& j, PhaseTransitionRecord& r);

} // namespace merak::worldbuilding
