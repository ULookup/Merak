#pragma once
#include <merak/worldbuilding/pipeline_workflow_def.hpp>
#include <functional>
#include <map>

namespace pqxx { class connection; }

namespace merak::worldbuilding {

// Condition evaluation function signature
// Input: ConditionDef (definition), PipelineState (current state), pqxx::connection (DB)
// Output: ConditionResult (evaluation result)
using ConditionEvalFn = std::function<ConditionResult(
    const ConditionDef& cond,
    const PipelineState& state,
    pqxx::connection& conn
)>;

class ConditionEvaluator {
public:
    static ConditionEvaluator& instance();

    // Register a custom condition type
    void register_condition(const std::string& type, ConditionEvalFn fn);

    // Evaluate a single condition
    ConditionResult evaluate(const ConditionDef& cond,
                             const PipelineState& state,
                             pqxx::connection& conn) const;

    // Evaluate a condition group (with AND/OR logic)
    ConditionEvalSummary evaluate_group(const ConditionGroup& group,
                                        const PipelineState& state,
                                        pqxx::connection& conn,
                                        const std::string& phase_key) const;

private:
    ConditionEvaluator();
    void register_builtins();
    std::map<std::string, ConditionEvalFn> registry_;
};

// ─── 9 built-in condition evaluation functions ───

// "entity_count" — SELECT COUNT(*) FROM {entity} WHERE world_id=$1 [AND kind=$2]
ConditionResult eval_entity_count(const ConditionDef& cond,
                                  const PipelineState& state,
                                  pqxx::connection& conn);

// "all_characters_have_cards" — all 'individual' agents have character_cards
ConditionResult eval_all_characters_have_cards(const ConditionDef& cond,
                                               const PipelineState& state,
                                               pqxx::connection& conn);

// "world_has_rule_system" — world_knowledge has category='rules' entries
ConditionResult eval_world_has_rule_system(const ConditionDef& cond,
                                           const PipelineState& state,
                                           pqxx::connection& conn);

// "scene_count_in_chapter" — scenes in active_chapter >= $total_scenes_target
ConditionResult eval_scene_count_in_chapter(const ConditionDef& cond,
                                            const PipelineState& state,
                                            pqxx::connection& conn);

// "all_scenes_ended" — all scenes in active_chapter are completed + have diary
ConditionResult eval_all_scenes_ended(const ConditionDef& cond,
                                      const PipelineState& state,
                                      pqxx::connection& conn);

// "all_checks_passed" — run named check items
ConditionResult eval_all_checks_passed(const ConditionDef& cond,
                                       const PipelineState& state,
                                       pqxx::connection& conn);

// "has_more_chapters" — are there draft/planned chapters remaining in the arc?
ConditionResult eval_has_more_chapters(const ConditionDef& cond,
                                       const PipelineState& state,
                                       pqxx::connection& conn);

// "user_confirmed" — checks pipeline_states.user_confirm flag
ConditionResult eval_user_confirmed(const ConditionDef& cond,
                                    const PipelineState& state,
                                    pqxx::connection& conn);

// "custom_sql" — execute arbitrary SQL, compare result with target_int
ConditionResult eval_custom_sql(const ConditionDef& cond,
                                const PipelineState& state,
                                pqxx::connection& conn);

} // namespace merak::worldbuilding
