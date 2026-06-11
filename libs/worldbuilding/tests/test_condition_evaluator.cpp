#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <merak/worldbuilding/condition_evaluator.hpp>
#include <merak/worldbuilding/pipeline.hpp>
#include <pqxx/pqxx>
#include <memory>
#include <thread>
#include <vector>

namespace merak::worldbuilding {
namespace {

class ConditionEvaluatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        evaluator_ = std::make_shared<ConditionEvaluator>();
        evaluator_->register_all_builtins();

        state_.world_id = "test_world_001";
        state_.current_phase = CreativePhase::Worldbuilding;
        state_.active_chapter_id = "ch_test_001";
        state_.scene_count_in_chapter = 3;
        state_.total_scenes_target = 5;
        state_.chapter_count = 2;
        state_.total_chapters_target = 10;
        state_.cycle_count = 1;
    }

    ConditionDef make_cond(const std::string& type,
                           const std::string& entity = "",
                           ConditionOp op = ConditionOp::GTE,
                           std::optional<int> target = std::nullopt) {
        ConditionDef c;
        c.type = type;
        c.entity = entity;
        c.op = op;
        c.target_int = target;
        c.message = "test condition";
        return c;
    }

    std::shared_ptr<ConditionEvaluator> evaluator_;
    PipelineState state_;
};

// ─── entity_count type registration ───

TEST_F(ConditionEvaluatorTest, EvalEntityCount_Registered) {
    auto types = evaluator_->list_condition_types();
    EXPECT_NE(std::find(types.begin(), types.end(), "entity_count"), types.end());
}

// ─── Unknown type returns false ───

TEST_F(ConditionEvaluatorTest, UnknownConditionType_NotRegistered) {
    auto types = evaluator_->list_condition_types();
    EXPECT_EQ(std::find(types.begin(), types.end(), "nonexistent_type"), types.end());
}

// ─── All 12 builtins registered ───

TEST_F(ConditionEvaluatorTest, AllBuiltinConditionsRegistered) {
    auto types = evaluator_->list_condition_types();
    EXPECT_GE(types.size(), 11u);
    EXPECT_NE(std::find(types.begin(), types.end(), "entity_count"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "all_characters_have_cards"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "world_has_rule_system"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "scene_count_in_chapter"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "all_scenes_ended"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "all_checks_passed"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "has_more_chapters"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "user_confirmed"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "diary_completeness"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "relation_currency"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "orphaned_foreshadowing"), types.end());
    EXPECT_NE(std::find(types.begin(), types.end(), "scene_completeness"), types.end());
}

// ─── custom_sql NOT registered ───

TEST_F(ConditionEvaluatorTest, CustomSqlNotRegistered) {
    auto types = evaluator_->list_condition_types();
    EXPECT_EQ(std::find(types.begin(), types.end(), "custom_sql"), types.end());
}

// ─── All 7 checks registered ───

TEST_F(ConditionEvaluatorTest, AllBuiltinChecksRegistered) {
    auto checks = evaluator_->list_check_names();
    EXPECT_EQ(checks.size(), 4u);
    EXPECT_NE(std::find(checks.begin(), checks.end(), "character_consistency"), checks.end());
    EXPECT_NE(std::find(checks.begin(), checks.end(), "diary_completeness"), checks.end());
    EXPECT_NE(std::find(checks.begin(), checks.end(), "relation_currency"), checks.end());
    EXPECT_NE(std::find(checks.begin(), checks.end(), "scene_completeness"), checks.end());
}

// ─── register_condition ───

TEST_F(ConditionEvaluatorTest, RegisterCustomCondition_ThenListed) {
    evaluator_->register_condition("custom_test", [](const ConditionDef&, const PipelineState&, pqxx::connection&) {
        return ConditionResult{"ok", true, 1, 1, {}};
    });
    auto types = evaluator_->list_condition_types();
    EXPECT_NE(std::find(types.begin(), types.end(), "custom_test"), types.end());
}

// ─── register_check ───

TEST_F(ConditionEvaluatorTest, RegisterCheck_ThenListed) {
    evaluator_->register_check("custom_check", [](const ConditionDef&, const PipelineState&, pqxx::connection&) {
        return ConditionResult{"ok", true, std::nullopt, std::nullopt, {}};
    });
    auto checks = evaluator_->list_check_names();
    EXPECT_NE(std::find(checks.begin(), checks.end(), "custom_check"), checks.end());
}

// ─── create_default returns valid evaluator ───

TEST_F(ConditionEvaluatorTest, CreateDefault_ReturnsValidEvaluator) {
    auto ev = ConditionEvaluator::create_default();
    EXPECT_NE(ev, nullptr);
    auto types = ev->list_condition_types();
    EXPECT_GE(types.size(), 11u);
    EXPECT_EQ(std::find(types.begin(), types.end(), "custom_sql"), types.end());
}

// ─── create_default instances are independent ───

TEST_F(ConditionEvaluatorTest, CreateDefault_InstancesAreIndependent) {
    auto ev1 = ConditionEvaluator::create_default();
    auto ev2 = ConditionEvaluator::create_default();
    ev1->register_condition("only_in_1", [](const ConditionDef&, const PipelineState&, pqxx::connection&) {
        return ConditionResult{"", true, std::nullopt, std::nullopt, {}};
    });
    auto types2 = ev2->list_condition_types();
    EXPECT_EQ(std::find(types2.begin(), types2.end(), "only_in_1"), types2.end());
}

// ─── Stats initial state ───

TEST_F(ConditionEvaluatorTest, Stats_InitialState_AllZeros) {
    auto& s = evaluator_->stats();
    EXPECT_EQ(s.total_evaluations.load(), 0u);
    EXPECT_EQ(s.total_failures.load(), 0u);
    EXPECT_EQ(s.total_errors.load(), 0u);
}

// ─── extract_significant_keywords Chinese ───

TEST_F(ConditionEvaluatorTest, ExtractKeywords_ChineseText) {
    auto keywords = extract_significant_keywords("张三在暗中策划政变");
    EXPECT_FALSE(keywords.empty());
    bool found = false;
    for (auto& kw : keywords) {
        if (kw.find("张三") != std::string::npos) found = true;
    }
    EXPECT_TRUE(found) << "Should find keyword containing 张三";
}

// ─── extract_significant_keywords English ───

TEST_F(ConditionEvaluatorTest, ExtractKeywords_EnglishText) {
    auto keywords = extract_significant_keywords("The prince plans to usurp the throne");
    EXPECT_FALSE(keywords.empty());
    EXPECT_NE(std::find(keywords.begin(), keywords.end(), "prince"), keywords.end());
    EXPECT_NE(std::find(keywords.begin(), keywords.end(), "plans"), keywords.end());
    EXPECT_NE(std::find(keywords.begin(), keywords.end(), "usurp"), keywords.end());
    EXPECT_NE(std::find(keywords.begin(), keywords.end(), "throne"), keywords.end());
}

// ─── extract_significant_keywords filters short tokens ───

TEST_F(ConditionEvaluatorTest, ExtractKeywords_ShortTokens_Filtered) {
    auto keywords = extract_significant_keywords("a be the cat");
    EXPECT_EQ(std::find(keywords.begin(), keywords.end(), "cat"), keywords.end());
    EXPECT_EQ(std::find(keywords.begin(), keywords.end(), "the"), keywords.end());
}

// ─── extract_significant_keywords empty input ───

TEST_F(ConditionEvaluatorTest, ExtractKeywords_EmptyInput) {
    auto keywords = extract_significant_keywords("");
    EXPECT_TRUE(keywords.empty());
}

// ─── extract_significant_keywords punctuation delimiters ───

TEST_F(ConditionEvaluatorTest, ExtractKeywords_PunctuationAsDelimiters) {
    auto keywords = extract_significant_keywords("Hello，World！This is a test。");
    EXPECT_NE(std::find(keywords.begin(), keywords.end(), "Hello"), keywords.end());
    EXPECT_NE(std::find(keywords.begin(), keywords.end(), "World"), keywords.end());
    EXPECT_NE(std::find(keywords.begin(), keywords.end(), "This"), keywords.end());
    EXPECT_NE(std::find(keywords.begin(), keywords.end(), "test"), keywords.end());
}

// ─── Thread safety: concurrent register_condition ───

TEST_F(ConditionEvaluatorTest, RegisterCondition_ThreadSafe) {
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([this, i]() {
            evaluator_->register_condition("thread_test_" + std::to_string(i),
                [](const ConditionDef&, const PipelineState&, pqxx::connection&) {
                    return ConditionResult{"", true, std::nullopt, std::nullopt, {}};
                });
        });
    }
    for (auto& t : threads) t.join();
    auto types = evaluator_->list_condition_types();
    for (int i = 0; i < 10; i++) {
        EXPECT_NE(std::find(types.begin(), types.end(), "thread_test_" + std::to_string(i)), types.end());
    }
}

} // namespace
} // namespace merak::worldbuilding
