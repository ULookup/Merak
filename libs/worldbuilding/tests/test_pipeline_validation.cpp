#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <merak/worldbuilding/pipeline_workflow_def.hpp>

// ─── Forward-declare validation types and functions ───
// (Implementation comes in Task 7: pipeline_validation.cpp)

namespace merak::worldbuilding {

struct PipelineValidationError {
    std::string file_path;
    std::string field;
    std::string message;
    enum Severity { ERROR, WARNING } severity;
};

std::vector<PipelineValidationError> validate_workflow_def(
    const PipelineWorkflowDef& def, const std::string& file_path);

PipelineWorkflowDef make_test_workflow();

} // namespace merak::worldbuilding

// ═══════════════════════════════════════════════════════════════════════════════
//  Test suite: PipelineValidationTest
// ═══════════════════════════════════════════════════════════════════════════════

namespace merak::worldbuilding {
namespace {

// ─── Helper: check if any result has the given severity and (optionally) a
//     substring match on the `field` member ──────────────────────────────────

bool has_error(const std::vector<PipelineValidationError>& results,
               PipelineValidationError::Severity severity,
               const std::string& field_substr = "") {
    for (const auto& e : results) {
        if (e.severity == severity &&
            (field_substr.empty() || e.field.find(field_substr) != std::string::npos))
            return true;
    }
    return false;
}

// ─── Helper: check if any result *message* contains the given text ─────────

bool has_message(const std::vector<PipelineValidationError>& results,
                 const std::string& text,
                 PipelineValidationError::Severity severity = PipelineValidationError::ERROR) {
    for (const auto& e : results) {
        if (e.severity == severity && e.message.find(text) != std::string::npos)
            return true;
    }
    return false;
}

// ══════════════════════════════════════════════════════════════════════════
//  12 TDD tests for pipeline validation rules
// ══════════════════════════════════════════════════════════════════════════

// ─── 1. make_test_workflow() produces a clean workflow ───────────────────

TEST(PipelineValidationTest, ValidWorkflow_PassesAllChecks) {
    auto wf = make_test_workflow();
    auto results = validate_workflow_def(wf, "test.json");

    for (const auto& e : results) {
        EXPECT_NE(e.severity, PipelineValidationError::ERROR)
            << "Unexpected ERROR on field '" << e.field << "': " << e.message;
    }
}

// ─── 2. Empty name → ERROR on field "name" ───────────────────────────────

TEST(PipelineValidationTest, EmptyName_ReportsError) {
    PipelineWorkflowDef wf;
    wf.name = "";
    wf.description = "test workflow";
    wf.phases = {PhaseDefinition{.key = "p1", .label = "Phase 1", .initial = true}};

    auto results = validate_workflow_def(wf, "test.json");
    EXPECT_TRUE(has_error(results, PipelineValidationError::ERROR, "name"));
}

// ─── 3. No phases → ERROR on field "phases" ──────────────────────────────

TEST(PipelineValidationTest, NoPhases_ReportsError) {
    PipelineWorkflowDef wf;
    wf.name = "test_workflow";
    wf.description = "has no phases";
    // wf.phases remains empty

    auto results = validate_workflow_def(wf, "test.json");
    EXPECT_TRUE(has_error(results, PipelineValidationError::ERROR, "phases"));
}

// ─── 4. Duplicate phase keys → ERROR mentioning "duplicate" ──────────────

TEST(PipelineValidationTest, DuplicatePhaseKeys_ReportsError) {
    PipelineWorkflowDef wf;
    wf.name = "test_workflow";
    wf.description = "duplicate keys";
    wf.phases = {
        PhaseDefinition{.key = "worldbuilding", .label = "Worldbuilding", .initial = true},
        PhaseDefinition{.key = "worldbuilding", .label = "Worldbuilding Dup"},
    };

    auto results = validate_workflow_def(wf, "test.json");
    EXPECT_TRUE(has_message(results, "duplicate"));
}

// ─── 5. Two initial phases → ERROR mentioning "multiple phases marked as
//       initial" ──────────────────────────────────────────────────────────

TEST(PipelineValidationTest, MultipleInitialPhases_ReportsError) {
    PipelineWorkflowDef wf;
    wf.name = "test_workflow";
    wf.description = "two initials";
    wf.phases = {
        PhaseDefinition{.key = "p1", .label = "Phase 1", .initial = true},
        PhaseDefinition{.key = "p2", .label = "Phase 2", .initial = true},
    };

    auto results = validate_workflow_def(wf, "test.json");
    EXPECT_TRUE(has_message(results, "multiple phases marked as initial"));
}

// ─── 6. No initial phase → WARNING mentioning "no initial phase" ─────────

TEST(PipelineValidationTest, NoInitialPhase_ReportsWarning) {
    PipelineWorkflowDef wf;
    wf.name = "test_workflow";
    wf.description = "no initial";
    wf.phases = {
        PhaseDefinition{.key = "p1", .label = "Phase 1", .initial = false},
    };

    auto results = validate_workflow_def(wf, "test.json");
    EXPECT_TRUE(has_message(results, "no initial phase", PipelineValidationError::WARNING));
}

// ─── 7. allowed_retreat references nonexistent key → ERROR mentioning
//       "does not exist" ──────────────────────────────────────────────────

TEST(PipelineValidationTest, RetreatToNonExistentPhase_ReportsError) {
    PipelineWorkflowDef wf;
    wf.name = "test_workflow";
    wf.description = "bad retreat";
    wf.phases = {
        PhaseDefinition{
            .key = "p1",
            .label = "Phase 1",
            .initial = true,
            .allowed_retreat = {"nonexistent_phase"},
        },
    };

    auto results = validate_workflow_def(wf, "test.json");
    EXPECT_TRUE(has_message(results, "does not exist"));
}

// ─── 8. auto_loop.continue_while with wrong token count → ERROR on field
//       containing "auto_loop" ────────────────────────────────────────────

TEST(PipelineValidationTest, InvalidAutoLoopExpression_ReportsError) {
    PhaseDefinition p1;
    p1.key = "p1";
    p1.label = "Phase 1";
    p1.initial = true;
    p1.auto_loop = AutoLoopDef{
        .entity = "chapter",
        .target = "all_scenes_in_chapter",
        .continue_while = "scene_count <",  // only 2 tokens — invalid
    };

    PipelineWorkflowDef wf;
    wf.name = "test_workflow";
    wf.description = "bad auto_loop expression";
    wf.phases = {std::move(p1)};

    auto results = validate_workflow_def(wf, "test.json");
    EXPECT_TRUE(has_error(results, PipelineValidationError::ERROR, "auto_loop"));
}

// ─── 9. auto_loop with unknown operator "??" → ERROR mentioning "unknown
//       operator" ─────────────────────────────────────────────────────────

TEST(PipelineValidationTest, UnknownOperatorInAutoLoop_ReportsError) {
    PhaseDefinition p1;
    p1.key = "p1";
    p1.label = "Phase 1";
    p1.initial = true;
    p1.auto_loop = AutoLoopDef{
        .entity = "chapter",
        .target = "all_scenes_in_chapter",
        .continue_while = "scene_count ?? total_scenes_target",
    };

    PipelineWorkflowDef wf;
    wf.name = "test_workflow";
    wf.description = "unknown operator";
    wf.phases = {std::move(p1)};

    auto results = validate_workflow_def(wf, "test.json");
    EXPECT_TRUE(has_message(results, "unknown operator"));
}

// ─── 10. auto_loop with unknown LHS field → ERROR mentioning "unknown
//        field" ───────────────────────────────────────────────────────────

TEST(PipelineValidationTest, UnknownFieldInAutoLoop_ReportsError) {
    PhaseDefinition p1;
    p1.key = "p1";
    p1.label = "Phase 1";
    p1.initial = true;
    p1.auto_loop = AutoLoopDef{
        .entity = "chapter",
        .target = "all_scenes_in_chapter",
        .continue_while = "unknown_field >= total_scenes_target",
    };

    PipelineWorkflowDef wf;
    wf.name = "test_workflow";
    wf.description = "unknown field";
    wf.phases = {std::move(p1)};

    auto results = validate_workflow_def(wf, "test.json");
    EXPECT_TRUE(has_message(results, "unknown field"));
}

// ─── 11. Condition with empty type → ERROR mentioning ".type" and
//         "empty" ─────────────────────────────────────────────────────────

TEST(PipelineValidationTest, EmptyConditionType_ReportsError) {
    ConditionDef bad_cond;
    bad_cond.type = "";
    bad_cond.entity = "agent";
    bad_cond.message = "should not be empty";

    ConditionGroup group;
    group.operator_type = "and";
    group.conditions = {std::move(bad_cond)};

    PipelineWorkflowDef wf;
    wf.name = "test_workflow";
    wf.description = "empty condition type";
    wf.phases = {
        PhaseDefinition{
            .key = "p1",
            .label = "Phase 1",
            .initial = true,
            .advance_when = std::move(group),
        },
    };

    auto results = validate_workflow_def(wf, "test.json");
    EXPECT_TRUE(has_message(results, ".type"));
    EXPECT_TRUE(has_message(results, "empty"));
}

// ─── 12. Condition with empty message → ERROR mentioning ".message" and
//         "empty" ─────────────────────────────────────────────────────────

TEST(PipelineValidationTest, EmptyConditionMessage_ReportsError) {
    ConditionDef bad_cond;
    bad_cond.type = "entity_count";
    bad_cond.entity = "agent";
    bad_cond.message = "";  // empty message

    ConditionGroup group;
    group.operator_type = "and";
    group.conditions = {std::move(bad_cond)};

    PipelineWorkflowDef wf;
    wf.name = "test_workflow";
    wf.description = "empty condition message";
    wf.phases = {
        PhaseDefinition{
            .key = "p1",
            .label = "Phase 1",
            .initial = true,
            .advance_when = std::move(group),
        },
    };

    auto results = validate_workflow_def(wf, "test.json");
    EXPECT_TRUE(has_message(results, ".message"));
    EXPECT_TRUE(has_message(results, "empty"));
}

} // namespace
} // namespace merak::worldbuilding
