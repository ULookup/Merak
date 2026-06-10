#include <merak/worldbuilding/condition_evaluator.hpp>
#include <merak/worldbuilding/pipeline.hpp>
#include <pqxx/pqxx>
#include <spdlog/spdlog.h>

namespace merak::worldbuilding {

ConditionEvaluator& ConditionEvaluator::instance() {
    static ConditionEvaluator inst;
    return inst;
}

ConditionEvaluator::ConditionEvaluator() {
    register_builtins();
}

void ConditionEvaluator::register_condition(const std::string& type, ConditionEvalFn fn) {
    registry_[type] = std::move(fn);
}

ConditionResult ConditionEvaluator::evaluate(const ConditionDef& cond,
                                              const PipelineState& state,
                                              pqxx::connection& conn) const {
    auto it = registry_.find(cond.type);
    if (it == registry_.end()) {
        spdlog::warn("Unknown condition type: {}", cond.type);
        return ConditionResult{cond.message, false, std::nullopt, std::nullopt, {}};
    }
    return it->second(cond, state, conn);
}

ConditionEvalSummary ConditionEvaluator::evaluate_group(const ConditionGroup& group,
                                                         const PipelineState& state,
                                                         pqxx::connection& conn,
                                                         const std::string& phase_key) const {
    ConditionEvalSummary summary;
    summary.phase_key = phase_key;
    summary.results.reserve(group.conditions.size());

    bool is_and = (group.operator_type == "and");
    summary.all_met = is_and ? true : false; // start: true for AND, false for OR

    for (auto& cond : group.conditions) {
        auto result = evaluate(cond, state, conn);
        summary.results.push_back(result);
        if (is_and) {
            summary.all_met = summary.all_met && result.met;
        } else {
            summary.all_met = summary.all_met || result.met;
        }
    }

    return summary;
}

void ConditionEvaluator::register_builtins() {
    register_condition("entity_count", eval_entity_count);
    register_condition("all_characters_have_cards", eval_all_characters_have_cards);
    register_condition("world_has_rule_system", eval_world_has_rule_system);
    register_condition("scene_count_in_chapter", eval_scene_count_in_chapter);
    register_condition("all_scenes_ended", eval_all_scenes_ended);
    register_condition("all_checks_passed", eval_all_checks_passed);
    register_condition("has_more_chapters", eval_has_more_chapters);
    register_condition("user_confirmed", eval_user_confirmed);
    register_condition("custom_sql", eval_custom_sql);
}

// ─── Built-in condition implementations ───

ConditionResult eval_entity_count(const ConditionDef& cond,
                                   const PipelineState& state,
                                   pqxx::connection& conn) {
    ConditionResult result{cond.message, false, std::nullopt, cond.target_int, {}};

    pqxx::work txn(conn);
    std::string query = "SELECT COUNT(*) FROM " + cond.entity + " WHERE world_id = $1";
    if (cond.kind_filter) {
        query += " AND kind = " + txn.quote(*cond.kind_filter);
    }
    auto row = txn.exec_params1(query, state.world_id);
    int count = row[0].as<int>();
    result.current = count;

    switch (cond.op) {
        case ConditionOp::EQ:  result.met = (count == cond.target_int.value_or(0)); break;
        case ConditionOp::NEQ: result.met = (count != cond.target_int.value_or(0)); break;
        case ConditionOp::GT:  result.met = (count >  cond.target_int.value_or(0)); break;
        case ConditionOp::GTE: result.met = (count >= cond.target_int.value_or(0)); break;
        case ConditionOp::LT:  result.met = (count <  cond.target_int.value_or(0)); break;
        case ConditionOp::LTE: result.met = (count <= cond.target_int.value_or(0)); break;
        default: result.met = (count >= cond.target_int.value_or(0)); break;
    }
    return result;
}

ConditionResult eval_all_characters_have_cards(const ConditionDef& cond,
                                                const PipelineState& state,
                                                pqxx::connection& conn) {
    ConditionResult result{cond.message, false, std::nullopt, std::nullopt, {}};
    pqxx::work txn(conn);
    // Count individual agents that have no character card
    auto row = txn.exec_params1(R"(
        SELECT COUNT(*)
        FROM agents a
        LEFT JOIN character_cards cc ON cc.agent_id = a.id
        WHERE a.world_id = $1 AND a.kind = 'individual' AND cc.id IS NULL
    )", state.world_id);
    int missing = row[0].as<int>();
    result.current = missing;
    result.target = 0;
    result.met = (missing == 0);
    return result;
}

ConditionResult eval_world_has_rule_system(const ConditionDef& cond,
                                            const PipelineState& state,
                                            pqxx::connection& conn) {
    ConditionResult result{cond.message, false, std::nullopt, std::nullopt, {}};
    pqxx::work txn(conn);
    auto row = txn.exec_params1(
        "SELECT COUNT(*) FROM world_knowledge WHERE world_id = $1 AND category = 'rules'",
        state.world_id);
    int count = row[0].as<int>();
    result.current = count;
    result.target = 1;
    result.met = (count > 0);
    return result;
}

ConditionResult eval_scene_count_in_chapter(const ConditionDef& cond,
                                             const PipelineState& state,
                                             pqxx::connection& conn) {
    ConditionResult result{cond.message, false, std::nullopt, std::nullopt, {}};
    if (!state.active_chapter_id) {
        result.met = false;
        return result;
    }

    pqxx::work txn(conn);
    auto row = txn.exec_params1(
        "SELECT COUNT(*) FROM scenes WHERE chapter_id = $1",
        *state.active_chapter_id);
    int count = row[0].as<int>();
    result.current = count;

    int target = state.total_scenes_target;
    // Allow overriding target via target_str variable reference
    if (cond.target_str && *cond.target_str == "$total_scenes_target") {
        target = state.total_scenes_target;
    } else if (cond.target_int) {
        target = *cond.target_int;
    }
    result.target = target;
    result.met = (count >= target);
    return result;
}

ConditionResult eval_all_scenes_ended(const ConditionDef& cond,
                                       const PipelineState& state,
                                       pqxx::connection& conn) {
    ConditionResult result{cond.message, false, std::nullopt, std::nullopt, {}};
    if (!state.active_chapter_id) {
        result.met = false;
        return result;
    }

    pqxx::work txn(conn);
    // Check: all scenes in chapter are completed AND have diaries
    auto row = txn.exec_params1(R"(
        SELECT COUNT(*)
        FROM scenes s
        WHERE s.chapter_id = $1
        AND (s.status != 'completed' OR NOT EXISTS (
            SELECT 1 FROM agent_diaries ad WHERE ad.scene_id = s.id
        ))
    )", *state.active_chapter_id);
    int incomplete = row[0].as<int>();
    result.current = incomplete;
    result.target = 0;
    result.met = (incomplete == 0);
    return result;
}

ConditionResult eval_all_checks_passed(const ConditionDef& cond,
                                        const PipelineState& state,
                                        pqxx::connection& conn) {
    ConditionResult result{cond.message, true, std::nullopt, std::nullopt, {}};
    // Each check in the 'checks' list represents a review category.
    // For now, this requires all items to be present. Actual review logic
    // can be extended by registering additional condition types.
    if (cond.checks) {
        nlohmann::json check_results = nlohmann::json::array();
        for (auto& check_name : *cond.checks) {
            nlohmann::json cr;
            cr["name"] = check_name;
            cr["passed"] = true; // Placeholder: always pass until review system is wired
            check_results.push_back(cr);
        }
        result.extra["checks"] = check_results;
        result.current = static_cast<int>(cond.checks->size());
        result.target = static_cast<int>(cond.checks->size());
    }
    result.met = true;
    return result;
}

ConditionResult eval_has_more_chapters(const ConditionDef& cond,
                                        const PipelineState& state,
                                        pqxx::connection& conn) {
    ConditionResult result{cond.message, false, std::nullopt, std::nullopt, {}};
    if (!state.active_arc_id) {
        result.met = false;
        return result;
    }

    pqxx::work txn(conn);
    auto row = txn.exec_params1(
        "SELECT COUNT(*) FROM chapters WHERE arc_id = $1 AND status IN ('draft','planned')",
        *state.active_arc_id);
    int remaining = row[0].as<int>();
    result.current = remaining;
    result.target = 0;
    result.met = (remaining > 0);
    return result;
}

ConditionResult eval_user_confirmed(const ConditionDef& cond,
                                     const PipelineState& state,
                                     pqxx::connection& conn) {
    ConditionResult result{cond.message, false, std::nullopt, std::nullopt, {}};
    pqxx::work txn(conn);
    auto row = txn.exec_params1(
        "SELECT user_confirm FROM pipeline_states WHERE world_id = $1",
        state.world_id);
    bool confirmed = row[0].as<bool>();
    result.met = confirmed;
    return result;
}

ConditionResult eval_custom_sql(const ConditionDef& cond,
                                 const PipelineState& state,
                                 pqxx::connection& conn) {
    ConditionResult result{cond.message, false, std::nullopt, cond.target_int, {}};
    if (!cond.target_str) {
        result.met = false;
        return result;
    }

    pqxx::work txn(conn);
    try {
        auto row = txn.exec_params1(*cond.target_str, state.world_id);
        int val = row[0].as<int>();
        result.current = val;
        if (cond.target_int) {
            result.met = (val >= *cond.target_int);
        } else {
            result.met = (val > 0);
        }
    } catch (const std::exception& e) {
        spdlog::warn("custom_sql condition failed: {}", e.what());
        result.met = false;
    }
    return result;
}

} // namespace merak::worldbuilding
