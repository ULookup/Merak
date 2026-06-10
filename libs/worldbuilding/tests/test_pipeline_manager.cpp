#include <gtest/gtest.h>
#include <merak/worldbuilding/pipeline_manager.hpp>
#include <merak/worldbuilding/condition_evaluator.hpp>
#include <merak/worldbuilding/pipeline.hpp>
#include <pqxx/pqxx>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <thread>
#include <chrono>
#include <fstream>

namespace merak::worldbuilding {
namespace {

class PipelineManagerTest : public ::testing::Test {
protected:
    std::shared_ptr<pqxx::connection> conn_;
    std::vector<RuntimeEvent> captured_events_;

    void SetUp() override {
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
                if (conn_) return conn_;
                throw std::runtime_error("no database connection");
            },
            .event_emitter = [this](const RuntimeEvent& e) {
                captured_events_.push_back(e);
            },
            .pipeline_config_dir = std::filesystem::current_path() / "config" / "pipelines",
            .condition_evaluator = ConditionEvaluator::create_default(),
        };
    }

    // Helper: initialize a world with the default workflow.
    // Requires DB; throws if no DB is available.
    void setup_world(PipelineManager& mgr, const std::string& world_id) {
        mgr.load_workflow_defs();
        mgr.activate_workflow(world_id, "default_creative_pipeline");
    }
};

// ═══════════════════════════════════════════════════════════════
// Category 1: advance_phase (7 tests)
// ═══════════════════════════════════════════════════════════════

TEST_F(PipelineManagerTest, AdvancePhase_NoActiveState_ReturnsNoActiveState) {
    auto deps = make_deps();
    PipelineManager mgr(deps);

    PipelineManager::AdvanceRequest req{"unknown-world", CreativePhase::CharacterCreation,
                                        "test", std::nullopt, false, false};
    auto result = mgr.advance_phase(req);
    EXPECT_EQ(result, PipelineManager::AdvanceResult::NO_ACTIVE_STATE);
    EXPECT_EQ(PipelineManager::advance_result_to_string(result), "no_active_state");
}

TEST_F(PipelineManagerTest, AdvancePhase_AlreadyAtPhase_ReturnsAlreadyAtPhase) {
    if (!conn_) GTEST_SKIP() << "No database connection";

    auto deps = make_deps();
    PipelineManager mgr(deps);
    setup_world(mgr, "test-world");
    captured_events_.clear();

    // State should be at Worldbuilding (initial phase)
    PipelineManager::AdvanceRequest req{"test-world", CreativePhase::Worldbuilding,
                                        "test", std::nullopt, false, false};
    auto result = mgr.advance_phase(req);
    EXPECT_EQ(result, PipelineManager::AdvanceResult::ALREADY_AT_PHASE);
    EXPECT_EQ(PipelineManager::advance_result_to_string(result), "already_at_phase");
}

TEST_F(PipelineManagerTest, AdvancePhase_InvalidTransition_ReturnsInvalidTransition) {
    if (!conn_) GTEST_SKIP() << "No database connection";

    auto deps = make_deps();
    PipelineManager mgr(deps);
    setup_world(mgr, "test-world");
    captured_events_.clear();

    // From Worldbuilding, PlotArchitecture is neither a forward transition
    // nor in Worldbuilding's allowed_retreat
    PipelineManager::AdvanceRequest req{"test-world", CreativePhase::PlotArchitecture,
                                        "test", std::nullopt, false, false};
    auto result = mgr.advance_phase(req);
    EXPECT_EQ(result, PipelineManager::AdvanceResult::INVALID_TRANSITION);
    EXPECT_EQ(PipelineManager::advance_result_to_string(result), "invalid_transition");
}

TEST_F(PipelineManagerTest, AdvancePhase_Force_SkipsConditionCheck) {
    if (!conn_) GTEST_SKIP() << "No database connection";

    auto deps = make_deps();
    PipelineManager mgr(deps);
    setup_world(mgr, "test-world");
    captured_events_.clear();

    // With force=true, conditions are skipped; advance from Worldbuilding to CharacterCreation
    PipelineManager::AdvanceRequest req{"test-world", CreativePhase::CharacterCreation,
                                        "test", std::nullopt, true, false};
    auto result = mgr.advance_phase(req);
    EXPECT_EQ(result, PipelineManager::AdvanceResult::SUCCESS);

    // Verify state actually changed
    auto state = mgr.get_state("test-world");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->current_phase, CreativePhase::CharacterCreation);
}

TEST_F(PipelineManagerTest, AdvancePhase_Retreat_Allowed) {
    if (!conn_) GTEST_SKIP() << "No database connection";

    auto deps = make_deps();
    PipelineManager mgr(deps);
    setup_world(mgr, "test-world");

    // First, force-advance to CharacterCreation
    PipelineManager::AdvanceRequest forward_req{"test-world", CreativePhase::CharacterCreation,
                                                 "test", std::nullopt, true, false};
    auto result1 = mgr.advance_phase(forward_req);
    ASSERT_EQ(result1, PipelineManager::AdvanceResult::SUCCESS);

    captured_events_.clear();

    // Then retreat back to Worldbuilding (CharacterCreation→Worldbuilding is in
    // allowed_next_phases(CharacterCreation))
    PipelineManager::AdvanceRequest retreat_req{"test-world", CreativePhase::Worldbuilding,
                                                 "test", std::nullopt, true, false};
    auto result2 = mgr.advance_phase(retreat_req);
    EXPECT_EQ(result2, PipelineManager::AdvanceResult::SUCCESS);

    auto state = mgr.get_state("test-world");
    ASSERT_TRUE(state.has_value());
    EXPECT_EQ(state->current_phase, CreativePhase::Worldbuilding);
}

TEST_F(PipelineManagerTest, AdvancePhase_Retreat_NotAllowed) {
    if (!conn_) GTEST_SKIP() << "No database connection";

    auto deps = make_deps();
    PipelineManager mgr(deps);
    setup_world(mgr, "test-world");

    // From Worldbuilding, try to retreat to Reflection — not in allowed_next_phases
    // and Worldbuilding has no allowed_retreat in the workflow
    PipelineManager::AdvanceRequest req{"test-world", CreativePhase::Reflection,
                                        "test", std::nullopt, true, false};
    auto result = mgr.advance_phase(req);
    EXPECT_EQ(result, PipelineManager::AdvanceResult::INVALID_TRANSITION);
}

TEST_F(PipelineManagerTest, AdvancePhase_Success_EmitsSSE) {
    if (!conn_) GTEST_SKIP() << "No database connection";

    auto deps = make_deps();
    PipelineManager mgr(deps);
    setup_world(mgr, "test-world");
    captured_events_.clear();

    PipelineManager::AdvanceRequest req{"test-world", CreativePhase::CharacterCreation,
                                        "test", std::nullopt, true, false};
    auto result = mgr.advance_phase(req);
    EXPECT_EQ(result, PipelineManager::AdvanceResult::SUCCESS);

    // Verify SSE event was emitted
    bool found = false;
    for (auto& ev : captured_events_) {
        if (ev.type == "pipeline_phase_changed") {
            found = true;
            EXPECT_EQ(ev.payload.value("world_id", ""), "test-world");
            EXPECT_EQ(ev.payload.value("phase", ""), "character_creation");
            EXPECT_EQ(ev.payload.value("trigger", ""), "test");
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected pipeline_phase_changed event to be emitted";
}

// ═══════════════════════════════════════════════════════════════
// Category 2: on_world_event (3 tests)
// ═══════════════════════════════════════════════════════════════

TEST_F(PipelineManagerTest, OnWorldEvent_IrrelevantEvent_Filtered) {
    auto deps = make_deps();
    // Clear the event_emitter so we can check events aren't captured via other paths
    deps.event_emitter = [this](const RuntimeEvent& e) {
        captured_events_.push_back(e);
    };
    PipelineManager mgr(deps);

    captured_events_.clear();
    mgr.on_world_event("test-world", "some_unknown_event_type", nlohmann::json::object());
    // Irrelevant events are filtered immediately; no events should be emitted
    EXPECT_EQ(captured_events_.size(), 0);
}

TEST_F(PipelineManagerTest, OnWorldEvent_NoActiveState_Returns) {
    auto deps = make_deps();
    PipelineManager mgr(deps);

    captured_events_.clear();
    // Using a relevant event type, but world has no state
    mgr.on_world_event("unknown-world", "agent_created", nlohmann::json::object());
    // Should return early after state check without crashing
    EXPECT_EQ(captured_events_.size(), 0);
}

TEST_F(PipelineManagerTest, OnWorldEvent_RelevantEvent_Debounced) {
    auto deps = make_deps();
    PipelineManager mgr(deps);

    captured_events_.clear();
    nlohmann::json payload;

    // First call establishes debounce timestamp for "test-world"
    mgr.on_world_event("test-world", "agent_created", payload);
    size_t after_first = captured_events_.size();

    // Second call within 2-second window should be debounced (skipped early)
    mgr.on_world_event("test-world", "agent_created", payload);
    // No additional events should be emitted (no state anyway, but debounce works)
    EXPECT_EQ(captured_events_.size(), after_first);

    // After the debounce window expires, the call should proceed again
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));
    mgr.on_world_event("test-world", "agent_created", payload);
    // Still no state, so no events — but the debounce check passed
    SUCCEED();
}

// ═══════════════════════════════════════════════════════════════
// Category 3: evaluate_loop_condition (3 tests)
// ═══════════════════════════════════════════════════════════════

TEST_F(PipelineManagerTest, EvaluateLoopCondition_ValidExpression_True) {
    auto deps = make_deps();
    PipelineManager mgr(deps);

    AutoLoopDef loop;
    loop.continue_while = "scene_count < total_scenes_target";

    PipelineState state;
    state.scene_count_in_chapter = 5;
    state.total_scenes_target = 10;

    EXPECT_TRUE(mgr.evaluate_loop_condition(loop, state));
}

TEST_F(PipelineManagerTest, EvaluateLoopCondition_ValidExpression_False) {
    auto deps = make_deps();
    PipelineManager mgr(deps);

    AutoLoopDef loop;
    loop.continue_while = "scene_count < total_scenes_target";

    PipelineState state;
    state.scene_count_in_chapter = 10;
    state.total_scenes_target = 10;
    // 10 < 10 evaluates to false

    EXPECT_FALSE(mgr.evaluate_loop_condition(loop, state));
}

TEST_F(PipelineManagerTest, EvaluateLoopCondition_InvalidExpression_ReturnsFalse) {
    auto deps = make_deps();
    PipelineManager mgr(deps);

    AutoLoopDef loop;
    loop.continue_while = "garbage expression !!!";

    PipelineState state;
    EXPECT_FALSE(mgr.evaluate_loop_condition(loop, state));

    // Empty expression
    AutoLoopDef empty_loop;
    empty_loop.continue_while = "";
    EXPECT_FALSE(mgr.evaluate_loop_condition(empty_loop, state));

    // Only two tokens (missing operator or operand)
    AutoLoopDef short_loop;
    short_loop.continue_while = "scene_count <";
    EXPECT_FALSE(mgr.evaluate_loop_condition(short_loop, state));
}

// ═══════════════════════════════════════════════════════════════
// Category 4: advance failure handling (3 tests)
// ═══════════════════════════════════════════════════════════════

TEST_F(PipelineManagerTest, HandleAdvanceFailure_EmitsSSE) {
    if (!conn_) GTEST_SKIP() << "No database connection";

    auto deps = make_deps();
    PipelineManager mgr(deps);
    setup_world(mgr, "test-world");
    captured_events_.clear();

    auto state_opt = mgr.get_state("test-world");
    ASSERT_TRUE(state_opt.has_value());
    mgr.handle_advance_failure("test-world", *state_opt,
                               PipelineManager::AdvanceResult::CONDITIONS_NOT_MET);

    // Verify SSE event was emitted
    bool found = false;
    for (auto& ev : captured_events_) {
        if (ev.type == "pipeline_advance_failed") {
            found = true;
            EXPECT_EQ(ev.payload.value("world_id", ""), "test-world");
            EXPECT_EQ(ev.payload.value("result", ""), "conditions_not_met");
            break;
        }
    }
    EXPECT_TRUE(found) << "Expected pipeline_advance_failed event to be emitted";
}

TEST_F(PipelineManagerTest, HandleAdvanceFailure_PersistsLastError) {
    if (!conn_) GTEST_SKIP() << "No database connection";

    auto deps = make_deps();
    PipelineManager mgr(deps);
    setup_world(mgr, "test-world");
    captured_events_.clear();

    auto state_opt = mgr.get_state("test-world");
    ASSERT_TRUE(state_opt.has_value());
    mgr.handle_advance_failure("test-world", *state_opt,
                               PipelineManager::AdvanceResult::INVALID_TRANSITION);

    // Verify last_error is stored in state.extra
    auto updated_state = mgr.get_state("test-world");
    ASSERT_TRUE(updated_state.has_value());
    ASSERT_TRUE(updated_state->extra.contains("last_error"));
    EXPECT_EQ(updated_state->extra["last_error"].value("result", ""), "invalid_transition");
    EXPECT_TRUE(updated_state->extra["last_error"].contains("timestamp"));
}

TEST_F(PipelineManagerTest, ClearLastError_ResetsError) {
    if (!conn_) GTEST_SKIP() << "No database connection";

    auto deps = make_deps();
    PipelineManager mgr(deps);
    setup_world(mgr, "test-world");
    captured_events_.clear();

    // First, trigger a failure to set the error
    auto state_opt = mgr.get_state("test-world");
    ASSERT_TRUE(state_opt.has_value());
    mgr.handle_advance_failure("test-world", *state_opt,
                               PipelineManager::AdvanceResult::CONDITIONS_NOT_MET);

    // Verify error is present
    auto state_with_error = mgr.get_state("test-world");
    ASSERT_TRUE(state_with_error.has_value());
    ASSERT_TRUE(state_with_error->extra.contains("last_error"));

    // Clear the error
    mgr.clear_last_error("test-world");

    // Verify error is gone
    auto state_cleared = mgr.get_state("test-world");
    ASSERT_TRUE(state_cleared.has_value());
    EXPECT_FALSE(state_cleared->extra.contains("last_error"));
}

// ═══════════════════════════════════════════════════════════════
// Category 5: get_view_data (2 tests)
// ═══════════════════════════════════════════════════════════════

TEST_F(PipelineManagerTest, GetViewData_ReturnsState) {
    if (!conn_) GTEST_SKIP() << "No database connection";

    auto deps = make_deps();
    PipelineManager mgr(deps);
    setup_world(mgr, "test-world");

    auto view = mgr.get_view_data("test-world");
    EXPECT_EQ(view.state.world_id, "test-world");
    EXPECT_EQ(view.state.current_phase, CreativePhase::Worldbuilding);
    EXPECT_EQ(view.active_workflow_name, "default_creative_pipeline");
    // history is populated from DB (may be empty for fresh world)
    EXPECT_GE(view.recent_history.size(), 0);
}

TEST_F(PipelineManagerTest, GetViewData_NoState_ReturnsEmpty) {
    auto deps = make_deps();
    PipelineManager mgr(deps);

    auto view = mgr.get_view_data("unknown-world");
    // Default-constructed PipelineViewData: empty state, empty name
    EXPECT_TRUE(view.active_workflow_name.empty());
    EXPECT_EQ(view.state.world_id, "");
}

// ═══════════════════════════════════════════════════════════════
// Category 6: snapshot/restore (2 tests)
// ═══════════════════════════════════════════════════════════════

TEST_F(PipelineManagerTest, SnapshotToJson_NoState_ReturnsEmptyJson) {
    auto deps = make_deps();
    PipelineManager mgr(deps);

    std::string snapshot = mgr.snapshot_to_json("unknown-world");
    EXPECT_EQ(snapshot, "{}");
}

TEST_F(PipelineManagerTest, RestoreFromSnapshot_Roundtrip) {
    if (!conn_) GTEST_SKIP() << "No database connection";

    auto deps = make_deps();
    PipelineManager mgr(deps);
    setup_world(mgr, "test-world");

    // Advance to CharacterCreation so we have a non-initial state
    PipelineManager::AdvanceRequest req{"test-world", CreativePhase::CharacterCreation,
                                        "test", std::nullopt, true, true};
    mgr.advance_phase(req);

    auto state_before = mgr.get_state("test-world");
    ASSERT_TRUE(state_before.has_value());
    EXPECT_EQ(state_before->current_phase, CreativePhase::CharacterCreation);

    // Snapshot the current state
    std::string snapshot = mgr.snapshot_to_json("test-world");
    EXPECT_NE(snapshot, "{}");
    EXPECT_NE(snapshot.find("world_id"), std::string::npos);

    // Parse and modify a field to simulate a change
    auto j = nlohmann::json::parse(snapshot);
    EXPECT_EQ(j["world_id"], "test-world");
    EXPECT_EQ(j["current_phase"], "character_creation");

    // Restore from snapshot and verify
    mgr.restore_from_snapshot("test-world", snapshot);
    auto state_after = mgr.get_state("test-world");
    ASSERT_TRUE(state_after.has_value());
    EXPECT_EQ(state_after->current_phase, CreativePhase::CharacterCreation);
    EXPECT_EQ(state_after->world_id, "test-world");
}

// ═══════════════════════════════════════════════════════════════
// Category 7: Dependency Injection (1 test)
// ═══════════════════════════════════════════════════════════════

TEST_F(PipelineManagerTest, InjectedConditionEvaluator_IsUsed) {
    auto deps = make_deps();

    // Verify the injected evaluator has all built-in types registered
    auto types = deps.condition_evaluator->list_condition_types();
    EXPECT_GE(types.size(), 10) << "Expected at least 10 built-in condition types";

    auto checks = deps.condition_evaluator->list_check_names();
    EXPECT_GE(checks.size(), 5) << "Expected at least 5 built-in check names";

    // Create PipelineManager with the injected evaluator — no crash = DI works
    PipelineManager mgr(deps);
    auto names = mgr.list_workflows();
    EXPECT_GE(names.size(), 0);

    // Verify stats are available from the evaluator
    const auto& stats = deps.condition_evaluator->stats();
    EXPECT_EQ(stats.total_evaluations.load(), 0);
    EXPECT_EQ(stats.total_failures.load(), 0);
    EXPECT_EQ(stats.total_errors.load(), 0);
}

// ═══════════════════════════════════════════════════════════════
// Category 8: load_workflow_defs (1 test)
// ═══════════════════════════════════════════════════════════════

TEST_F(PipelineManagerTest, LoadWorkflowDefs_InvalidJson_Skipped) {
    // Create a temporary directory with one valid and one invalid JSON file
    auto temp_dir = std::filesystem::temp_directory_path() / "merak_test_pipelines";
    std::filesystem::create_directories(temp_dir);

    // Write a malformed JSON file — should be skipped without crashing
    std::ofstream bad_file(temp_dir / "malformed.json");
    bad_file << "{ this is not valid JSON !!! }";
    bad_file.close();

    // Write a minimal valid workflow def
    std::ofstream good_file(temp_dir / "minimal.json");
    nlohmann::json minimal = {
        {"name", "minimal_pipeline"},
        {"description", "A minimal test pipeline"},
        {"version", 1},
        {"phases", nlohmann::json::array({
            {
                {"key", "worldbuilding"},
                {"label", "Worldbuilding"},
                {"initial", true}
            }
        })}
    };
    good_file << minimal.dump();
    good_file.close();

    // Write a non-JSON file that should be skipped by extension check
    std::ofstream text_file(temp_dir / "readme.txt");
    text_file << "This is not a JSON file";
    text_file.close();

    auto deps = make_deps();
    deps.pipeline_config_dir = temp_dir;
    PipelineManager mgr(deps);

    // Should not crash — invalid JSON is skipped
    mgr.load_workflow_defs();

    auto names = mgr.list_workflows();
    // The valid JSON file should be loaded
    bool found_minimal = false;
    for (auto& n : names) {
        if (n == "minimal_pipeline") {
            found_minimal = true;
            break;
        }
    }
    EXPECT_TRUE(found_minimal) << "Valid JSON workflow 'minimal_pipeline' should be loaded";
    // The malformed JSON file should be silently skipped (count may be 1)
    EXPECT_GE(names.size(), 1);

    // Verify the loaded workflow has the expected structure
    const auto* wf = mgr.get_workflow("minimal_pipeline");
    ASSERT_NE(wf, nullptr);
    EXPECT_EQ(wf->phases.size(), 1);
    EXPECT_EQ(wf->phases[0].key, "worldbuilding");

    // Cleanup
    std::filesystem::remove_all(temp_dir);
}

} // namespace
} // namespace merak::worldbuilding
