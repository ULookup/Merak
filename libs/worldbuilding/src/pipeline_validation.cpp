#include <merak/worldbuilding/pipeline_validation.hpp>
#include <merak/worldbuilding/pipeline_workflow_def.hpp>
#include <merak/worldbuilding/pipeline.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <functional>
#include <set>
#include <sstream>
#include <vector>
#include <map>

namespace merak::worldbuilding {

// ═══════════════════════════════════════════════════════════════
// validate_workflow_def（设计文档第 4 节）
// 12 ERROR rules + 4 WARNING rules
// ═══════════════════════════════════════════════════════════════
std::vector<PipelineValidationError> validate_workflow_def(
    const PipelineWorkflowDef& def, const std::string& file_path) {

    std::vector<PipelineValidationError> errors;
    auto err = [&](const std::string& field, const std::string& msg,
                   PipelineValidationError::Severity sev = PipelineValidationError::ERROR) {
        errors.push_back({file_path, field, msg, sev});
    };

    // ─── E1: name non-empty ───────────────────────────────────────
    if (def.name.empty()) {
        err("name", "workflow name is empty");
    }

    // ─── E2: phases non-empty ─────────────────────────────────────
    if (def.phases.empty()) {
        err("phases", "no phases defined");
        return errors; // 后续校验依赖 phases 非空
    }

    // ─── E3: unique phase keys ────────────────────────────────────
    std::set<std::string> keys;
    for (size_t i = 0; i < def.phases.size(); i++) {
        if (keys.count(def.phases[i].key)) {
            err("phases[" + std::to_string(i) + "].key",
                "duplicate phase key: " + def.phases[i].key);
        }
        keys.insert(def.phases[i].key);
    }

    // ─── E4: exactly one initial phase ────────────────────────────
    int initial_count = 0;
    for (auto& p : def.phases) {
        if (p.initial) initial_count++;
    }
    if (initial_count == 0) {
        err("phases[0].initial",
            "no initial phase set, defaulting to first phase",
            PipelineValidationError::WARNING);
    } else if (initial_count > 1) {
        err("phases",
            "multiple phases marked as initial (" +
            std::to_string(initial_count) + "), expected exactly 1");
    }

    // ─── E5-E11: per-phase validation ─────────────────────────────
    for (size_t i = 0; i < def.phases.size(); i++) {
        auto& p = def.phases[i];

        // E6: allowed_retreat references exist
        for (auto& retreat : p.allowed_retreat) {
            if (!keys.count(retreat)) {
                err("phases[" + std::to_string(i) + "].allowed_retreat",
                    "retreat target '" + retreat + "' does not exist");
            }
        }

        // E7-E8: advance_when conditions validation
        if (p.advance_when) {
            for (size_t j = 0; j < p.advance_when->conditions.size(); j++) {
                auto& c = p.advance_when->conditions[j];
                std::string prefix = "phases[" + std::to_string(i) +
                                     "].advance_when.conditions[" + std::to_string(j) + "]";

                if (c.type.empty()) {
                    err(prefix + ".type", "condition.type is empty");
                }
                if (c.message.empty()) {
                    err(prefix + ".message", "condition.message is empty");
                }
                if (c.type == "all_checks_passed" && (!c.checks || c.checks->empty())) {
                    err(prefix + ".checks",
                        "all_checks_passed with empty checks list",
                        PipelineValidationError::WARNING);
                }
            }
        }

        // E9-E11: auto_loop expression parseable
        if (p.auto_loop) {
            std::string prefix = "phases[" + std::to_string(i) +
                                 "].auto_loop.continue_while";
            std::istringstream iss(p.auto_loop->continue_while);
            std::vector<std::string> tokens;
            std::string tok;
            while (iss >> tok) tokens.push_back(tok);

            if (tokens.size() != 3) {
                err(prefix, "expression must have exactly 3 tokens (field op value), got " +
                    std::to_string(tokens.size()));
            } else {
                // Validate operator — E10
                static const std::set<std::string> valid_ops =
                    {"<", ">", "<=", ">=", "==", "!="};
                if (!valid_ops.count(tokens[1])) {
                    err(prefix, "unknown operator: " + tokens[1]);
                }
                // Validate LHS field name — E11
                static const std::set<std::string> valid_fields = {
                    "scene_count", "total_scenes_target",
                    "chapter_count", "total_chapters_target", "cycle_count"
                };
                if (!valid_fields.count(tokens[0])) {
                    err(prefix, "unknown field: " + tokens[0]);
                }
            }
        }
    }

    // ─── E12: no circular retreat (DFS cycle detection) — WARNING ──
    {
        std::map<std::string, std::vector<std::string>> retreat_graph;
        for (auto& p : def.phases) {
            retreat_graph[p.key] = p.allowed_retreat;
        }

        for (auto& [key, _] : retreat_graph) {
            std::set<std::string> visited, in_stack;
            std::function<bool(const std::string&)> dfs =
                [&](const std::string& node) -> bool {
                visited.insert(node);
                in_stack.insert(node);
                for (auto& next : retreat_graph[node]) {
                    if (!visited.count(next)) {
                        if (dfs(next)) return true;
                    } else if (in_stack.count(next)) {
                        return true;
                    }
                }
                in_stack.erase(node);
                return false;
            };

            if (!visited.count(key) && dfs(key)) {
                err("phases.allowed_retreat",
                    "circular retreat detected involving phase: " + key,
                    PipelineValidationError::WARNING);
                break; // 只报告一次
            }
        }
    }

    return errors;
}

// ═══════════════════════════════════════════════════════════════
// make_test_workflow — 用于测试的合法 workflow 构建函数
// ═══════════════════════════════════════════════════════════════
PipelineWorkflowDef make_test_workflow() {
    PipelineWorkflowDef wf;
    wf.name = "test_workflow";
    wf.description = "A valid test workflow";
    wf.version = 1;

    PhaseDefinition p1;
    p1.key = "worldbuilding";
    p1.label = "Worldbuilding";
    p1.initial = true;
    p1.advance_when = ConditionGroup{"and", {
        ConditionDef{"entity_count", "agents", std::nullopt,
                     ConditionOp::GTE, 1, std::nullopt,
                     std::nullopt, "at least 1 agent"}
    }};

    PhaseDefinition p2;
    p2.key = "character_creation";
    p2.label = "Character Creation";
    p2.allowed_retreat = {"worldbuilding"};

    PhaseDefinition p3;
    p3.key = "reflection";
    p3.label = "Reflection";
    p3.allowed_retreat = {"character_creation"};

    wf.phases = {p1, p2, p3};
    return wf;
}

} // namespace merak::worldbuilding
