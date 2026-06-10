#include <gtest/gtest.h>
#include <merak/worldbuilding/condition_evaluator.hpp>
#include <merak/worldbuilding/pipeline_workflow_def.hpp>
#include <merak/worldbuilding/pipeline.hpp>
#include <pqxx/pqxx>
#include <nlohmann/json.hpp>
#include <fstream>

namespace merak::worldbuilding {
namespace {

class ConditionEvaluatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Use an in-memory test setup or skip if no PG available
        // For unit tests, we test JSON parsing + logic paths
    }
};

// Test: JSON deserialization of full workflow file
TEST_F(ConditionEvaluatorTest, ParseDefaultPipelineJson) {
    // This test verifies the JSON config can be fully parsed
    // without a PG connection
    std::ifstream f("config/pipelines/default_creative_pipeline.json");
    if (!f.is_open()) {
        GTEST_SKIP() << "Config file not found, skipping JSON parse test";
    }
    nlohmann::json j = nlohmann::json::parse(f);
    ASSERT_TRUE(j.contains("phases"));

    auto wf = j.get<PipelineWorkflowDef>();
    EXPECT_EQ(wf.name, "default_creative_pipeline");
    EXPECT_EQ(wf.phases.size(), 5);
    EXPECT_TRUE(wf.auto_advance);

    // Verify initial phase
    const auto* initial = wf.initial_phase();
    ASSERT_NE(initial, nullptr);
    EXPECT_EQ(initial->key, "worldbuilding");
    EXPECT_TRUE(initial->initial);

    // Verify worldbuilding phase has 3 conditions
    ASSERT_TRUE(initial->advance_when.has_value());
    EXPECT_EQ(initial->advance_when->conditions.size(), 3);
    EXPECT_EQ(initial->advance_when->operator_type, "and");

    // Verify scene_writing has auto_loop
    const auto* sw = wf.get_phase("scene_writing");
    ASSERT_NE(sw, nullptr);
    ASSERT_TRUE(sw->auto_loop.has_value());
    EXPECT_EQ(sw->auto_loop->entity, "chapter");

    // Verify reflection has on_complete conditional action
    const auto* rf = wf.get_phase("reflection");
    ASSERT_NE(rf, nullptr);
    ASSERT_EQ(rf->on_complete.size(), 1);
    EXPECT_EQ(rf->on_complete[0].type, "conditional");
}

// Test: ConditionOp from_string
TEST_F(ConditionEvaluatorTest, OpFromString) {
    EXPECT_EQ(op_from_string("=="), ConditionOp::EQ);
    EXPECT_EQ(op_from_string("eq"), ConditionOp::EQ);
    EXPECT_EQ(op_from_string(">="), ConditionOp::GTE);
    EXPECT_EQ(op_from_string("gte"), ConditionOp::GTE);
    EXPECT_EQ(op_from_string("<="), ConditionOp::LTE);
    EXPECT_EQ(op_from_string("lte"), ConditionOp::LTE);
    EXPECT_EQ(op_from_string("contains"), ConditionOp::CONTAINS);
    EXPECT_EQ(op_from_string("exists"), ConditionOp::EXISTS);
    EXPECT_EQ(op_from_string("unknown_blah"), ConditionOp::EQ); // default
}

// Test: PipelineState to_json / from_json roundtrip
TEST_F(ConditionEvaluatorTest, PipelineStateJsonRoundtrip) {
    PipelineState original;
    original.world_id = "test-world-001";
    original.current_phase = CreativePhase::CharacterCreation;
    original.active_workflow = "default_creative_pipeline";
    original.chapter_count = 5;
    original.cycle_count = 2;
    original.last_updated = "2026-06-09T12:00:00Z";

    nlohmann::json j = original;
    auto restored = j.get<PipelineState>();

    EXPECT_EQ(restored.world_id, original.world_id);
    EXPECT_EQ(restored.current_phase, original.current_phase);
    EXPECT_EQ(restored.active_workflow, original.active_workflow);
    EXPECT_EQ(restored.chapter_count, original.chapter_count);
    EXPECT_EQ(restored.cycle_count, original.cycle_count);
}

// Test: ConditionGroup JSON deserialization
TEST_F(ConditionEvaluatorTest, ParseConditionGroup) {
    std::string json = R"({
        "operator": "and",
        "conditions": [
            {"type": "entity_count", "entity": "agents", "op": ">=", "target_int": 2, "message": "at least 2 agents"},
            {"type": "world_has_rule_system", "message": "has rules"}
        ]
    })";
    auto j = nlohmann::json::parse(json);
    auto group = j.get<ConditionGroup>();

    EXPECT_EQ(group.operator_type, "and");
    EXPECT_EQ(group.conditions.size(), 2);
    EXPECT_EQ(group.conditions[0].type, "entity_count");
    EXPECT_EQ(group.conditions[0].entity, "agents");
    EXPECT_EQ(group.conditions[0].op, ConditionOp::GTE);
    EXPECT_EQ(group.conditions[0].target_int.value(), 2);
    EXPECT_EQ(group.conditions[1].type, "world_has_rule_system");
}

// Test: PhaseTransition JSON roundtrip
TEST_F(ConditionEvaluatorTest, PhaseTransitionJsonRoundtrip) {
    PhaseTransitionRecord record;
    record.id = "abc-123";
    record.world_id = "test-world";
    record.from_phase = CreativePhase::Worldbuilding;
    record.to_phase = CreativePhase::CharacterCreation;
    record.trigger = "auto";
    record.triggered_by = "agent_created";
    record.timestamp = "2026-06-09T12:00:00Z";

    nlohmann::json j;
    to_json(j, record);

    auto restored = j.get<PhaseTransitionRecord>();
    EXPECT_EQ(restored.id, record.id);
    EXPECT_EQ(restored.from_phase, record.from_phase);
    EXPECT_EQ(restored.to_phase, record.to_phase);
    EXPECT_EQ(restored.trigger, record.trigger);
    EXPECT_EQ(*restored.triggered_by, *record.triggered_by);
}

// Test: allowed_next_phases state machine
TEST_F(ConditionEvaluatorTest, AllowedNextPhasesStateMachine) {
    // worldbuilding → character_creation only
    auto next = allowed_next_phases(CreativePhase::Worldbuilding);
    ASSERT_EQ(next.size(), 1);
    EXPECT_EQ(next[0], CreativePhase::CharacterCreation);

    // character_creation → worldbuilding (retreat) or plot_architecture (advance)
    next = allowed_next_phases(CreativePhase::CharacterCreation);
    ASSERT_EQ(next.size(), 2);
    EXPECT_NE(std::find(next.begin(), next.end(), CreativePhase::Worldbuilding), next.end());
    EXPECT_NE(std::find(next.begin(), next.end(), CreativePhase::PlotArchitecture), next.end());

    // reflection → scene_writing only
    next = allowed_next_phases(CreativePhase::Reflection);
    ASSERT_EQ(next.size(), 1);
    EXPECT_EQ(next[0], CreativePhase::SceneWriting);
}

} // namespace
} // namespace merak::worldbuilding
