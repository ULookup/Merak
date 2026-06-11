#include <merak/worldbuilding/condition_evaluator.hpp>
#include <merak/worldbuilding/pipeline.hpp>
#include <merak/kg/kg_provider.hpp>
#include <pqxx/pqxx>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

namespace merak::worldbuilding {

// ═══════════════════════════════════════════════════════════════
// entity_count table name whitelist
// ═══════════════════════════════════════════════════════════════
namespace {
const std::set<std::string> VALID_KINDS = {
    "individual", "god", "group", "creature", "force",
    "organization", "location_bound", "abstract"
};

const std::map<std::string, std::string> ENTITY_TABLES = {
    {"agents", "agents"},
    {"locations", "locations"},
    {"chapters", "chapters"},
    {"scenes", "scenes"},
    {"arcs", "arcs"},
    {"agent_relations", "agent_relations"},
    {"foreshadowings", "foreshadowings"},
    {"agent_diaries", "agent_diaries"},
    {"character_cards", "character_cards"},
    {"world_knowledge", "world_knowledge"},
    {"secrets", "secrets"},
    {"timeline_events", "timeline_events"},
    {"memory_summaries", "memory_summaries"},
};
} // namespace

// ═══════════════════════════════════════════════════════════════
// Keyword extraction utility
// ═══════════════════════════════════════════════════════════════
std::vector<std::string> extract_significant_keywords(const std::string& text) {
    std::vector<std::string> keywords;
    std::string current;

    for (size_t i = 0; i < text.size(); ) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        size_t char_len = 1;
        if ((c & 0x80) == 0)        char_len = 1;
        else if ((c & 0xE0) == 0xC0) char_len = 2;
        else if ((c & 0xF0) == 0xE0) char_len = 3;
        else if ((c & 0xF8) == 0xF0) char_len = 4;

        std::string ch = text.substr(i, char_len);
        bool is_delim = (char_len == 1 && (isspace(c) || ispunct(c)))
                        || ch == "\xEF\xBC\x8C"      // ，
                        || ch == "\xE3\x80\x82"      // 。
                        || ch == "\xEF\xBC\x81"      // ！
                        || ch == "\xEF\xBC\x9F"      // ？
                        || ch == "\xE3\x80\x81"      // 、
                        || ch == "\xEF\xBC\x9B"      // ；
                        || ch == "\xEF\xBC\x9A"      // ：
                        || ch == "\xE3\x80\x8C"      // 「
                        || ch == "\xE3\x80\x8D"      // 」
                        || ch == "\xEF\xBC\x88"      // （
                        || ch == "\xEF\xBC\x89"      // ）
                        || ch == "\xE2\x80\x9C"      // "
                        || ch == "\xE2\x80\x9D"      // "
                        || ch == "\xE2\x80\xA6"      // …
                        || ch == "\xE2\x80\x94";     // —

        if (is_delim) {
            if (current.size() >= 4) keywords.push_back(current);
            current.clear();
        } else {
            current += ch;
        }
        i += char_len;
    }
    if (current.size() >= 4) keywords.push_back(current);
    return keywords;
}

// ═══════════════════════════════════════════════════════════════
// ConditionEvaluator class implementation
// ═══════════════════════════════════════════════════════════════

std::shared_ptr<ConditionEvaluator> ConditionEvaluator::create_default() {
    auto evaluator = std::make_shared<ConditionEvaluator>();
    evaluator->register_all_builtins();
    return evaluator;
}

void ConditionEvaluator::register_all_builtins() {
    std::unique_lock lock(registry_mutex_);

    // ─── condition types ───
    registry_["entity_count"] = eval_entity_count;
    registry_["all_characters_have_cards"] = eval_all_characters_have_cards;
    registry_["world_has_rule_system"] = eval_world_has_rule_system;
    registry_["scene_count_in_chapter"] = eval_scene_count_in_chapter;
    registry_["all_scenes_ended"] = eval_all_scenes_ended;
    registry_["has_more_chapters"] = eval_has_more_chapters;
    registry_["user_confirmed"] = eval_user_confirmed;
    registry_["diary_completeness"] = eval_diary_completeness;
    registry_["relation_currency"] = eval_relation_currency;
    registry_["orphaned_foreshadowing"] = eval_orphaned_foreshadowing;
    registry_["scene_completeness"] = eval_scene_completeness;

    // ─── KG condition types ───
    registry_["kg_relation_count"] = [this](const ConditionDef& cond,
                                             const PipelineState& state,
                                             pqxx::connection&) -> ConditionResult {
        ConditionResult result{cond.message, true, std::nullopt, std::nullopt, {}};
        if (!kg_provider_) {
            result.met = false;
            result.error = "KG provider not available";
            return result;
        }
        try {
            int threshold = cond.params.contains("min_count")
                ? cond.params["min_count"].get<int>() : 1;
            auto entities = kg_provider_->list_entities(state.world_id);
            // Count distinct entity names that appear in relations
            merak::kg::QueryFilters filters;
            auto sg = kg_provider_->query_subgraph(state.world_id, {}, filters);
            int count = static_cast<int>(sg.relations.size());
            result.met = (count >= threshold);
            result.extra["relation_count"] = count;
        } catch (const std::exception& e) {
            result.met = false;
            result.error = e.what();
        }
        return result;
    };

    registry_["kg_entity_has_relations"] = [this](const ConditionDef& cond,
                                                    const PipelineState& state,
                                                    pqxx::connection&) -> ConditionResult {
        ConditionResult result{cond.message, true, std::nullopt, std::nullopt, {}};
        if (!kg_provider_) {
            result.met = false;
            result.error = "KG provider not available";
            return result;
        }
        try {
            std::string entity_name = cond.params.value("entity_name", "");
            if (entity_name.empty()) {
                result.met = false;
                result.error = "entity_name parameter required";
                return result;
            }
            merak::kg::QueryFilters filters;
            auto ng = kg_provider_->expand(state.world_id, entity_name, 1, filters);
            result.met = !ng.relations.empty();
            result.extra["relation_count"] = static_cast<int>(ng.relations.size());
            result.extra["neighbor_count"] = static_cast<int>(ng.neighbor_entities.size());
        } catch (const std::exception& e) {
            result.met = false;
            result.error = e.what();
        }
        return result;
    };

    // ─── check types (for all_checks_passed dispatch) ───
    check_registry_["character_consistency"] = eval_character_consistency;
    check_registry_["diary_completeness"] = eval_diary_completeness;
    check_registry_["relation_currency"] = eval_relation_currency;
    check_registry_["scene_completeness"] = eval_scene_completeness;
}

void ConditionEvaluator::register_condition(const std::string& type, ConditionEvalFn fn) {
    std::unique_lock lock(registry_mutex_);
    registry_[type] = std::move(fn);
}

void ConditionEvaluator::register_check(const std::string& name, ConditionEvalFn fn) {
    std::unique_lock lock(registry_mutex_);
    check_registry_[name] = std::move(fn);
}

std::vector<std::string> ConditionEvaluator::list_condition_types() const {
    std::shared_lock lock(registry_mutex_);
    std::vector<std::string> types;
    for (auto& [key, _] : registry_) types.push_back(key);
    return types;
}

std::vector<std::string> ConditionEvaluator::list_check_names() const {
    std::shared_lock lock(registry_mutex_);
    std::vector<std::string> names;
    for (auto& [key, _] : check_registry_) names.push_back(key);
    return names;
}

ConditionResult ConditionEvaluator::evaluate(const ConditionDef& cond,
                                              const PipelineState& state,
                                              pqxx::connection& conn) const {
    stats_.total_evaluations++;

    // all_checks_passed: expand via check_registry_ directly
    if (cond.type == "all_checks_passed") {
        ConditionResult result{cond.message, true, std::nullopt, std::nullopt, {}};
        nlohmann::json check_results = nlohmann::json::array();

        if (!cond.checks || cond.checks->empty()) return result;

        std::shared_lock lock(registry_mutex_);
        for (auto& check_name : *cond.checks) {
            auto it = check_registry_.find(check_name);
            if (it == check_registry_.end()) {
                check_results.push_back({
                    {"name", check_name}, {"passed", false},
                    {"error", "unknown check: " + check_name}
                });
                result.met = false;
                continue;
            }
            auto sub_result = it->second(cond, state, conn);
            check_results.push_back({
                {"name", check_name}, {"passed", sub_result.met},
                {"current", sub_result.current}, {"target", sub_result.target}
            });
            if (!sub_result.met) result.met = false;
        }

        result.extra["checks"] = check_results;
        result.current = static_cast<int>(cond.checks->size());
        result.target = static_cast<int>(cond.checks->size());
        if (!result.met) stats_.total_failures++;
        return result;
    }

    ConditionEvalFn fn;
    {
        std::shared_lock lock(registry_mutex_);
        auto it = registry_.find(cond.type);
        if (it == registry_.end()) {
            spdlog::warn("ConditionEvaluator: unknown condition type '{}'", cond.type);
            stats_.total_failures++;
            ConditionResult result{cond.message, false, std::nullopt, std::nullopt, {}};
            result.extra["error"] = "unknown condition type: " + cond.type;
            return result;
        }
        fn = it->second;
    }

    try {
        auto result = fn(cond, state, conn);
        if (!result.met) stats_.total_failures++;
        stats_.last_eval_time = std::chrono::steady_clock::now();
        return result;
    } catch (const pqxx::sql_error& e) {
        spdlog::error("ConditionEvaluator: SQL error in '{}': {}", cond.type, e.what());
        stats_.total_errors++;
        ConditionResult result{cond.message, false, std::nullopt, std::nullopt, {}};
        result.extra["error"] = std::string("SQL error: ") + e.what();
        return result;
    } catch (const std::exception& e) {
        spdlog::error("ConditionEvaluator: exception in '{}': {}", cond.type, e.what());
        stats_.total_errors++;
        ConditionResult result{cond.message, false, std::nullopt, std::nullopt, {}};
        result.extra["error"] = std::string("evaluation error: ") + e.what();
        return result;
    }
}

ConditionEvalSummary ConditionEvaluator::evaluate_group(
    const ConditionGroup& group,
    const PipelineState& state,
    pqxx::connection& conn,
    const std::string& phase_key) const {

    ConditionEvalSummary summary;
    summary.phase_key = phase_key;
    summary.results.reserve(group.conditions.size());

    bool is_and = (group.operator_type == "and");
    summary.all_met = is_and;

    for (auto& cond : group.conditions) {
        auto result = evaluate(cond, state, conn);
        summary.results.push_back(result);
        if (is_and) {
            summary.all_met = summary.all_met && result.met;
            if (!summary.all_met) break;
        } else {
            summary.all_met = summary.all_met || result.met;
            if (summary.all_met) break;
        }
    }

    return summary;
}

// ═══════════════════════════════════════════════════════════════
// eval_entity_count — with whitelist validation
// ═══════════════════════════════════════════════════════════════
ConditionResult eval_entity_count(const ConditionDef& cond,
                                   const PipelineState& state,
                                   pqxx::connection& conn) {
    ConditionResult result{cond.message, false, std::nullopt, cond.target_int, {}};

    auto table_it = ENTITY_TABLES.find(cond.entity);
    if (table_it == ENTITY_TABLES.end()) {
        spdlog::warn("eval_entity_count: unknown entity '{}'", cond.entity);
        result.met = false;
        result.extra["error"] = "unknown entity type: " + cond.entity;
        return result;
    }

    if (cond.kind_filter) {
        if (!VALID_KINDS.count(*cond.kind_filter)) {
            spdlog::warn("eval_entity_count: unknown kind '{}'", *cond.kind_filter);
            result.met = false;
            result.extra["error"] = "unknown kind: " + *cond.kind_filter;
            return result;
        }
    }

    try {
        pqxx::read_transaction txn(conn);
        std::string query = "SELECT COUNT(*) FROM " + table_it->second + " WHERE world_id = $1";
        if (cond.kind_filter) {
            query += " AND kind = " + txn.quote(*cond.kind_filter);
        }
        auto row = txn.exec_params1(query, state.world_id);
        int count = row[0].as<int>();
        result.current = count;

        int target = cond.target_int.value_or(0);
        switch (cond.op) {
            case ConditionOp::EQ:  result.met = (count == target); break;
            case ConditionOp::NEQ: result.met = (count != target); break;
            case ConditionOp::GT:  result.met = (count >  target); break;
            case ConditionOp::GTE: result.met = (count >= target); break;
            case ConditionOp::LT:  result.met = (count <  target); break;
            case ConditionOp::LTE: result.met = (count <= target); break;
        }
    } catch (const pqxx::sql_error& e) {
        spdlog::error("eval_entity_count: query failed for '{}': {}", cond.entity, e.what());
        result.met = false;
        result.extra["error"] = std::string("query failed: ") + e.what();
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════
// eval_all_characters_have_cards
// ═══════════════════════════════════════════════════════════════
ConditionResult eval_all_characters_have_cards(const ConditionDef& cond,
                                                const PipelineState& state,
                                                pqxx::connection& conn) {
    ConditionResult result{cond.message, false, std::nullopt, std::nullopt, {}};
    try {
        pqxx::read_transaction txn(conn);
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
    } catch (const std::exception& e) {
        spdlog::error("eval_all_characters_have_cards: {}", e.what());
        result.extra["error"] = e.what();
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════
// eval_world_has_rule_system
// ═══════════════════════════════════════════════════════════════
ConditionResult eval_world_has_rule_system(const ConditionDef& cond,
                                            const PipelineState& state,
                                            pqxx::connection& conn) {
    ConditionResult result{cond.message, false, std::nullopt, std::nullopt, {}};
    try {
        pqxx::read_transaction txn(conn);
        auto row = txn.exec_params1(
            "SELECT COUNT(*) FROM world_knowledge WHERE world_id = $1 AND category = 'rules'",
            state.world_id);
        int count = row[0].as<int>();
        result.current = count;
        result.target = 1;
        result.met = (count > 0);
    } catch (const std::exception& e) {
        spdlog::error("eval_world_has_rule_system: {}", e.what());
        result.extra["error"] = e.what();
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════
// eval_scene_count_in_chapter
// ═══════════════════════════════════════════════════════════════
ConditionResult eval_scene_count_in_chapter(const ConditionDef& cond,
                                             const PipelineState& state,
                                             pqxx::connection& conn) {
    ConditionResult result{cond.message, false, std::nullopt, std::nullopt, {}};
    if (!state.active_chapter_id) {
        result.met = false;
        result.extra["reason"] = "no active chapter";
        return result;
    }
    try {
        pqxx::read_transaction txn(conn);
        auto row = txn.exec_params1(
            "SELECT COUNT(*) FROM scenes WHERE chapter_id = $1",
            *state.active_chapter_id);
        int count = row[0].as<int>();
        result.current = count;

        int target = state.total_scenes_target;
        if (cond.target_str && *cond.target_str == "$total_scenes_target") {
            target = state.total_scenes_target;
        } else if (cond.target_int) {
            target = *cond.target_int;
        }
        result.target = target;
        result.met = (count >= target);
    } catch (const std::exception& e) {
        spdlog::error("eval_scene_count_in_chapter: {}", e.what());
        result.extra["error"] = e.what();
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════
// eval_all_scenes_ended
// ═══════════════════════════════════════════════════════════════
ConditionResult eval_all_scenes_ended(const ConditionDef& cond,
                                       const PipelineState& state,
                                       pqxx::connection& conn) {
    ConditionResult result{cond.message, false, std::nullopt, std::nullopt, {}};
    if (!state.active_chapter_id) {
        result.met = false;
        result.extra["reason"] = "no active chapter";
        return result;
    }
    try {
        pqxx::read_transaction txn(conn);
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
    } catch (const std::exception& e) {
        spdlog::error("eval_all_scenes_ended: {}", e.what());
        result.extra["error"] = e.what();
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════
// eval_has_more_chapters
// ═══════════════════════════════════════════════════════════════
ConditionResult eval_has_more_chapters(const ConditionDef& cond,
                                        const PipelineState& state,
                                        pqxx::connection& conn) {
    ConditionResult result{cond.message, false, std::nullopt, std::nullopt, {}};
    if (!state.active_arc_id) {
        result.met = false;
        result.extra["reason"] = "no active arc";
        return result;
    }
    try {
        pqxx::read_transaction txn(conn);
        auto row = txn.exec_params1(
            "SELECT COUNT(*) FROM chapters WHERE arc_id = $1 AND status IN ('draft','planned')",
            *state.active_arc_id);
        int remaining = row[0].as<int>();
        result.current = remaining;
        result.target = 0;
        result.met = (remaining > 0);
    } catch (const std::exception& e) {
        spdlog::error("eval_has_more_chapters: {}", e.what());
        result.extra["error"] = e.what();
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════
// eval_user_confirmed
// ═══════════════════════════════════════════════════════════════
ConditionResult eval_user_confirmed(const ConditionDef& cond,
                                     const PipelineState& state,
                                     pqxx::connection& conn) {
    ConditionResult result{cond.message, false, std::nullopt, std::nullopt, {}};
    try {
        pqxx::read_transaction txn(conn);
        auto row = txn.exec_params1(
            "SELECT user_confirm FROM pipeline_states WHERE world_id = $1",
            state.world_id);
        bool confirmed = row[0].as<bool>();
        result.met = confirmed;
    } catch (const std::exception& e) {
        spdlog::error("eval_user_confirmed: {}", e.what());
        result.extra["error"] = e.what();
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════
// eval_scene_completeness
// ═══════════════════════════════════════════════════════════════
ConditionResult eval_scene_completeness(const ConditionDef& cond,
                                         const PipelineState& state,
                                         pqxx::connection& conn) {
    ConditionResult result{cond.message, false, std::nullopt, 0, {}};
    if (!state.active_chapter_id) {
        result.met = false;
        result.extra["reason"] = "no active chapter";
        return result;
    }
    try {
        pqxx::read_transaction txn(conn);
        auto draft_row = txn.exec_params1(R"(
            SELECT COUNT(*) FROM scenes
            WHERE chapter_id = $1 AND status = 'draft'
        )", *state.active_chapter_id);
        int draft_count = draft_row[0].as<int>();

        auto empty_row = txn.exec_params1(R"(
            SELECT COUNT(*) FROM scenes
            WHERE chapter_id = $1 AND status = 'completed'
            AND (narrative IS NULL OR narrative = '')
        )", *state.active_chapter_id);
        int empty_narrative = empty_row[0].as<int>();

        int total_incomplete = draft_count + empty_narrative;
        result.current = total_incomplete;
        result.target = 0;
        result.met = (total_incomplete == 0);
        result.extra["draft_count"] = draft_count;
        result.extra["empty_narrative_count"] = empty_narrative;
    } catch (const std::exception& e) {
        spdlog::error("eval_scene_completeness: {}", e.what());
        result.extra["error"] = e.what();
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════
// eval_diary_completeness
// ═══════════════════════════════════════════════════════════════
ConditionResult eval_diary_completeness(const ConditionDef& cond,
                                         const PipelineState& state,
                                         pqxx::connection& conn) {
    ConditionResult result{cond.message, false, std::nullopt, 0, {}};
    if (!state.active_chapter_id) {
        result.met = false;
        result.extra["reason"] = "no active chapter";
        return result;
    }
    try {
        pqxx::read_transaction txn(conn);
        // Assume scenes.participant_ids is a JSONB array
        auto row = txn.exec_params1(R"(
            WITH scene_participants AS (
                SELECT s.id AS scene_id,
                       jsonb_array_elements_text(s.participant_ids) AS agent_id
                FROM scenes s
                WHERE s.chapter_id = $1 AND s.status = 'completed'
            )
            SELECT COUNT(*) FROM scene_participants sp
            WHERE NOT EXISTS (
                SELECT 1 FROM agent_diaries ad
                WHERE ad.scene_id = sp.scene_id AND ad.agent_id = sp.agent_id
            )
        )", *state.active_chapter_id);
        int missing = row[0].as<int>();
        result.current = missing;
        result.target = 0;
        result.met = (missing == 0);
    } catch (const std::exception& e) {
        spdlog::error("eval_diary_completeness: {}", e.what());
        result.extra["error"] = e.what();
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════
// eval_relation_currency
// ═══════════════════════════════════════════════════════════════
ConditionResult eval_relation_currency(const ConditionDef& cond,
                                        const PipelineState& state,
                                        pqxx::connection& conn) {
    ConditionResult result{cond.message, false, std::nullopt, 0, {}};
    if (!state.active_chapter_id) {
        result.met = false;
        result.extra["reason"] = "no active chapter";
        return result;
    }
    try {
        pqxx::read_transaction txn(conn);
        auto first_scene = txn.exec_params1(R"(
            SELECT created_at FROM scenes
            WHERE chapter_id = $1 ORDER BY created_at ASC LIMIT 1
        )", *state.active_chapter_id);
        auto chapter_start = first_scene[0].as<std::string>();

        auto row = txn.exec_params1(R"(
            SELECT COUNT(*) FROM agent_relations ar
            JOIN agents a ON (ar.agent_a_id = a.id OR ar.agent_b_id = a.id)
            WHERE a.world_id = $1 AND ar.updated_at < $2
        )", state.world_id, chapter_start);
        int stale = row[0].as<int>();
        result.current = stale;
        result.target = 0;
        result.met = (stale == 0);
    } catch (const std::exception& e) {
        spdlog::error("eval_relation_currency: {}", e.what());
        result.extra["error"] = e.what();
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════
// eval_orphaned_foreshadowing
// ═══════════════════════════════════════════════════════════════
ConditionResult eval_orphaned_foreshadowing(const ConditionDef& cond,
                                              const PipelineState& state,
                                              pqxx::connection& conn) {
    ConditionResult result{cond.message, false, std::nullopt, 0, {}};
    try {
        pqxx::read_transaction txn(conn);
        auto row = txn.exec_params1(R"(
            SELECT COUNT(*) FROM foreshadowings
            WHERE world_id = $1 AND status = 'open' AND updated_at = created_at
        )", state.world_id);
        int orphaned = row[0].as<int>();
        result.current = orphaned;
        result.target = 0;
        result.met = (orphaned == 0);
    } catch (const std::exception& e) {
        spdlog::error("eval_orphaned_foreshadowing: {}", e.what());
        result.extra["error"] = e.what();
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════
// eval_character_consistency — diary + secret leak detection
// ═══════════════════════════════════════════════════════════════
ConditionResult eval_character_consistency(const ConditionDef& cond,
                                            const PipelineState& state,
                                            pqxx::connection& conn) {
    auto diary_result = eval_diary_completeness(cond, state, conn);
    if (!diary_result.met) return diary_result;

    ConditionResult result = diary_result;
    result.message = cond.message;
    nlohmann::json leak_suspects = nlohmann::json::array();

    try {
        pqxx::read_transaction txn(conn);
        auto rows = txn.exec_params(R"(
            WITH active_secrets AS (
                SELECT id, truth, aware_character_ids
                FROM secrets WHERE world_id = $1 AND status = 'active'
            ),
            recent_diaries AS (
                SELECT DISTINCT ON (agent_id) agent_id, content
                FROM agent_diaries
                WHERE agent_id IN (
                    SELECT id FROM agents WHERE world_id = $1 AND kind = 'individual'
                )
                ORDER BY agent_id, created_at DESC
            )
            SELECT
                a.id AS agent_id,
                a.name AS agent_name,
                s.id AS secret_id,
                s.truth,
                COALESCE(rd.content, '') AS diary_content
            FROM agents a
            CROSS JOIN active_secrets s
            LEFT JOIN recent_diaries rd ON rd.agent_id = a.id
            WHERE a.world_id = $1
              AND a.kind = 'individual'
              AND NOT (a.id = ANY(s.aware_character_ids))
        )", state.world_id);

        for (auto& row : rows) {
            auto truth = row["truth"].as<std::string>();
            auto diary = row["diary_content"].as<std::string>();
            if (diary.empty()) continue;

            auto keywords = extract_significant_keywords(truth);
            for (auto& kw : keywords) {
                if (kw.size() >= 4 && diary.find(kw) != std::string::npos) {
                    leak_suspects.push_back({
                        {"agent_id", row["agent_id"].as<std::string>()},
                        {"agent_name", row["agent_name"].as<std::string>()},
                        {"secret_id", row["secret_id"].as<std::string>()},
                        {"matched_keyword", kw}
                    });
                    break;
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("eval_character_consistency: secret leak check failed: {}", e.what());
        result.extra["secret_check_error"] = e.what();
    }

    result.extra["secret_leak_suspects"] = leak_suspects;
    result.extra["suspect_count"] = leak_suspects.size();
    return result;
}

} // namespace merak::worldbuilding
