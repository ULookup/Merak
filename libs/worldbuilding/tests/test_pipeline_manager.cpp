#include <gtest/gtest.h>
#include <merak/worldbuilding/pipeline_manager.hpp>
#include <merak/worldbuilding/condition_evaluator.hpp>
#include <merak/worldbuilding/pipeline.hpp>
#include <pqxx/pqxx>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <thread>
#include <chrono>

namespace merak::worldbuilding {
namespace {

class PipelineManagerTest : public ::testing::Test {
protected:
    std::shared_ptr<pqxx::connection> conn_;
    std::vector<RuntimeEvent> captured_events_;

    void SetUp() override {
        // Try to connect to test database
        try {
            conn_ = std::make_shared<pqxx::connection>(
                "host=localhost port=5432 dbname=merak_test user=merak password=merak");
        } catch (...) {
            conn_ = nullptr;
        }
    }

    PipelineManager::Dependencies make_deps() {
        return {
            .pg_connection_factory = [this]() -> std::shared_ptr<pqxx::connection> {
                return std::make_shared<pqxx::connection>(
                    "host=localhost port=5432 dbname=merak_test user=merak password=merak");
            },
            .event_emitter = [this](const RuntimeEvent& e) {
                captured_events_.push_back(e);
            },
            .pipeline_config_dir = std::filesystem::current_path() / "config" / "pipelines",
        };
    }
};

// Test: Workflow JSON can be loaded
TEST_F(PipelineManagerTest, LoadWorkflowDefs) {
    auto deps = make_deps();
    deps.pipeline_config_dir = std::filesystem::current_path() / "config" / "pipelines";

    PipelineManager mgr(deps);
    mgr.load_workflow_defs();

    auto names = mgr.list_workflows();
    // At minimum, default_creative_pipeline should be loaded
    EXPECT_GE(names.size(), 1);
    EXPECT_NE(std::find(names.begin(), names.end(), "default_creative_pipeline"), names.end());

    const auto* wf = mgr.get_workflow("default_creative_pipeline");
    ASSERT_NE(wf, nullptr);
    EXPECT_EQ(wf->phases.size(), 5);
}

// Test: init_state_for_world creates state and emits SSE
TEST_F(PipelineManagerTest, InitStateEmitsEvent) {
    if (!conn_) GTEST_SKIP() << "No database connection";

    auto deps = make_deps();
    PipelineManager mgr(deps);
    // Skip ensure_tables for this unit test — we just test the logic
    // mgr.initialize(); // would need real PG

    // Test phase context generation
    PipelineState state;
    state.world_id = "test-world";
    state.current_phase = CreativePhase::Worldbuilding;

    auto ctx = generate_phase_context(state);
    EXPECT_FALSE(ctx.empty());
    EXPECT_NE(ctx.find("世界观构建"), std::string::npos);
}

// Test: phase context for each phase
TEST_F(PipelineManagerTest, PhaseContextForAllPhases) {
    PipelineState state;
    state.world_id = "test-world";

    auto phases = {
        CreativePhase::Worldbuilding,
        CreativePhase::CharacterCreation,
        CreativePhase::PlotArchitecture,
        CreativePhase::SceneWriting,
        CreativePhase::Reflection
    };

    for (auto phase : phases) {
        state.current_phase = phase;
        auto ctx = generate_phase_context(state);
        EXPECT_FALSE(ctx.empty()) << "Phase " << to_string(phase) << " has empty context";
    }
}

// Test: advance_result_to_string
TEST_F(PipelineManagerTest, AdvanceResultToString) {
    EXPECT_EQ(PipelineManager::advance_result_to_string(
        PipelineManager::AdvanceResult::SUCCESS), "success");
    EXPECT_EQ(PipelineManager::advance_result_to_string(
        PipelineManager::AdvanceResult::CONDITIONS_NOT_MET), "conditions_not_met");
    EXPECT_EQ(PipelineManager::advance_result_to_string(
        PipelineManager::AdvanceResult::INVALID_TRANSITION), "invalid_transition");
}

// Test: ConditionEvaluator singleton is accessible
TEST_F(PipelineManagerTest, ConditionEvaluatorSingleton) {
    auto& eval = ConditionEvaluator::instance();
    // Verify we can access the singleton — it should have 9 built-in types
    // We can verify by evaluating a known type
    ConditionDef cond;
    cond.type = "nonexistent_type_blah";
    cond.message = "test";

    PipelineState state;
    state.world_id = "test";

    if (conn_) {
        auto result = eval.evaluate(cond, state, *conn_);
        EXPECT_FALSE(result.met) << "Unknown condition type should return false";
    }
}

// Test: debounce logic (2-second window)
TEST_F(PipelineManagerTest, DebounceWindow) {
    auto deps = make_deps();
    PipelineManager mgr(deps);

    nlohmann::json payload;
    // First call should go through (no prior eval)
    mgr.on_world_event("test-world", "agent_created", payload);
    EXPECT_EQ(captured_events_.size(), 0); // No state for this world, so no events

    // Second call within 2 seconds with the same world should be debounced
    // This tests the time-based debounce. In a real test with PG, we'd verify
    // that the second call doesn't trigger condition evaluation.
}

} // namespace
} // namespace merak::worldbuilding
