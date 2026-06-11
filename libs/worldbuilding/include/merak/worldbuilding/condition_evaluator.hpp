#pragma once
#include <merak/worldbuilding/pipeline_workflow_def.hpp>
#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

namespace pqxx { class connection; }
namespace merak::kg { class KnowledgeGraphProvider; }

namespace merak::worldbuilding {

using ConditionEvalFn = std::function<ConditionResult(
    const ConditionDef& cond,
    const PipelineState& state,
    pqxx::connection& conn
)>;

class ConditionEvaluator {
public:
    ConditionEvaluator() = default;

    // Factory: create default instance with all builtins registered
    static std::shared_ptr<ConditionEvaluator> create_default();

    // Knowledge Graph integration
    void set_kg_provider(merak::kg::KnowledgeGraphProvider* provider) { kg_provider_ = provider; }

    // Register all built-in condition types and checks (public, callable from tests)
    void register_all_builtins();

    // Register custom condition/check (thread-safe)
    void register_condition(const std::string& type, ConditionEvalFn fn);
    void register_check(const std::string& name, ConditionEvalFn fn);

    // Evaluate (const, thread-safe)
    ConditionResult evaluate(const ConditionDef& cond,
                             const PipelineState& state,
                             pqxx::connection& conn) const;

    ConditionEvalSummary evaluate_group(const ConditionGroup& group,
                                        const PipelineState& state,
                                        pqxx::connection& conn,
                                        const std::string& phase_key) const;

    // Query registered types (for validation and debugging)
    std::vector<std::string> list_condition_types() const;
    std::vector<std::string> list_check_names() const;

    // Statistics
    struct Stats {
        std::atomic<uint64_t> total_evaluations{0};
        std::atomic<uint64_t> total_failures{0};
        std::atomic<uint64_t> total_errors{0};
        mutable std::chrono::steady_clock::time_point last_eval_time{};
    };
    const Stats& stats() const { return stats_; }

private:
    mutable std::shared_mutex registry_mutex_;
    std::map<std::string, ConditionEvalFn> registry_;
    std::map<std::string, ConditionEvalFn> check_registry_;
    merak::kg::KnowledgeGraphProvider* kg_provider_ = nullptr;
    mutable Stats stats_;
};

// ─── 12 built-in condition evaluation function declarations ───

ConditionResult eval_entity_count(const ConditionDef& cond,
                                  const PipelineState& state,
                                  pqxx::connection& conn);

ConditionResult eval_all_characters_have_cards(const ConditionDef& cond,
                                               const PipelineState& state,
                                               pqxx::connection& conn);

ConditionResult eval_world_has_rule_system(const ConditionDef& cond,
                                           const PipelineState& state,
                                           pqxx::connection& conn);

ConditionResult eval_scene_count_in_chapter(const ConditionDef& cond,
                                            const PipelineState& state,
                                            pqxx::connection& conn);

ConditionResult eval_all_scenes_ended(const ConditionDef& cond,
                                      const PipelineState& state,
                                      pqxx::connection& conn);

ConditionResult eval_has_more_chapters(const ConditionDef& cond,
                                       const PipelineState& state,
                                       pqxx::connection& conn);

ConditionResult eval_user_confirmed(const ConditionDef& cond,
                                    const PipelineState& state,
                                    pqxx::connection& conn);

ConditionResult eval_diary_completeness(const ConditionDef& cond,
                                         const PipelineState& state,
                                         pqxx::connection& conn);

ConditionResult eval_relation_currency(const ConditionDef& cond,
                                        const PipelineState& state,
                                        pqxx::connection& conn);

ConditionResult eval_orphaned_foreshadowing(const ConditionDef& cond,
                                              const PipelineState& state,
                                              pqxx::connection& conn);

ConditionResult eval_scene_completeness(const ConditionDef& cond,
                                         const PipelineState& state,
                                         pqxx::connection& conn);

ConditionResult eval_character_consistency(const ConditionDef& cond,
                                            const PipelineState& state,
                                            pqxx::connection& conn);

// Keyword extraction utility
std::vector<std::string> extract_significant_keywords(const std::string& text);

} // namespace merak::worldbuilding
