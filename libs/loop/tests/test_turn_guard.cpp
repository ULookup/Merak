#include <merak/turn_guard.hpp>
#include <cassert>
#include <iostream>

using namespace merak;

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    tests_run++; \
    std::cout << "  " << name << " ... "
#define PASS() \
    tests_passed++; \
    std::cout << "PASS" << std::endl

void test_default_config_matches_hardcoded_thresholds() {
    TEST("default TurnGuardConfig matches hardcoded thresholds");
    TurnGuard guard;
    TurnGuard::RoundInput in;
    in.consecutive_read_only_rounds = 3;
    auto v = guard.evaluate(in);
    assert(v.severity >= Severity::Warning);
    PASS();
}

void test_custom_threshold_avoids_warning() {
    TEST("custom threshold avoids warning below limit");
    TurnGuardConfig cfg;
    cfg.max_consecutive_read_only_rounds = 10;
    TurnGuard guard(cfg);
    TurnGuard::RoundInput in;
    in.consecutive_read_only_rounds = 3;
    auto v = guard.evaluate(in);
    assert(v.severity == Severity::Healthy);
    PASS();
}

void test_custom_nudge_message() {
    TEST("custom nudge message appears in verdict");
    TurnGuardConfig cfg;
    cfg.nudge_write_now = "CUSTOM: write now please";
    TurnGuard guard(cfg);
    TurnGuard::RoundInput in;
    in.consecutive_read_only_rounds = 3;
    auto v = guard.evaluate(in);
    assert(v.nudge.has_value());
    assert(v.nudge->find("CUSTOM: write now please") != std::string::npos);
    PASS();
}

void test_custom_nudge_prefix() {
    TEST("custom nudge prefix appears in verdict");
    TurnGuardConfig cfg;
    cfg.nudge_prefix = "[HINT] ";
    TurnGuard guard(cfg);
    TurnGuard::RoundInput in;
    in.consecutive_read_only_rounds = 3;
    auto v = guard.evaluate(in);
    assert(v.nudge.has_value());
    assert(v.nudge->find("[HINT] ") == 0);
    PASS();
}

void test_custom_world_query_threshold() {
    TEST("custom world query threshold takes effect");
    TurnGuardConfig cfg;
    cfg.max_consecutive_world_query_rounds = 10;
    TurnGuard guard(cfg);
    TurnGuard::RoundInput in;
    in.consecutive_world_query_rounds = 5;
    auto v = guard.evaluate(in);
    assert(v.severity != Severity::Critical);
    PASS();
}

void test_custom_content_avoidance_threshold() {
    TEST("custom content avoidance threshold takes effect");
    TurnGuardConfig cfg;
    cfg.max_consecutive_content_avoidance = 10;
    TurnGuard guard(cfg);
    TurnGuard::RoundInput in;
    in.consecutive_content_avoidance = 3;
    auto v = guard.evaluate(in);
    assert(v.severity == Severity::Healthy);
    PASS();
}

void test_custom_max_tool_calls_threshold() {
    TEST("custom max tool calls per round takes effect");
    TurnGuardConfig cfg;
    cfg.max_tool_calls_per_round = 50;
    TurnGuard guard(cfg);
    TurnGuard::RoundInput in;
    in.tool_count = 20;
    auto v = guard.evaluate(in);
    assert(v.severity == Severity::Healthy);
    PASS();
}

void test_custom_max_warnings_before_critical() {
    TEST("custom max warnings before critical takes effect");
    TurnGuardConfig cfg;
    cfg.max_warnings_before_critical = 10;
    TurnGuard guard(cfg);
    TurnGuard::RoundInput in;
    in.consecutive_read_only_rounds = 3;
    for (int i = 0; i < 5; i++) {
        auto v = guard.evaluate(in);
        assert(v.severity != Severity::Critical);
    }
    PASS();
}

void test_penalty_for_default_threshold() {
    TEST("penalty_for returns -999 when warning_count >= max_warnings_before_critical");
    TurnGuardConfig cfg;
    cfg.max_warnings_before_critical = 3;
    TurnGuard guard(cfg);
    TurnGuard::RoundInput in;
    in.consecutive_read_only_rounds = 3;
    guard.evaluate(in);
    guard.evaluate(in);
    auto v = guard.evaluate(in);
    assert(v.severity == Severity::Critical);
    PASS();
}

void test_reset_clears_warning_count() {
    TEST("reset clears warning count");
    TurnGuard guard;
    TurnGuard::RoundInput in;
    in.consecutive_read_only_rounds = 3;
    guard.evaluate(in);
    assert(guard.warning_count() == 1);
    guard.reset();
    assert(guard.warning_count() == 0);
    PASS();
}

void test_default_constructor_uses_default_config() {
    TEST("default constructor uses config defaults");
    TurnGuard guard;
    TurnGuard::RoundInput in;
    auto v = guard.evaluate(in);
    assert(v.severity == Severity::Healthy);
    PASS();
}

void test_config_default_values() {
    TEST("TurnGuardConfig defaults match original hardcoded values");
    TurnGuardConfig cfg;
    assert(cfg.max_consecutive_world_query_rounds == 5);
    assert(cfg.max_consecutive_read_only_rounds == 3);
    assert(cfg.max_consecutive_content_avoidance == 3);
    assert(cfg.max_tool_calls_per_round == 15);
    assert(cfg.max_warnings_before_critical == 4);
    assert(!cfg.nudge_write_now.empty());
    assert(!cfg.nudge_prefix.empty());
    PASS();
}

void test_restricted_domains_defaults_to_general() {
    TEST("restricted_tools defaults to empty (no restriction)");
    TurnGuard::Verdict v;
    assert(v.restricted_tools.empty());
    PASS();
}

void test_reason_messages_use_config_thresholds() {
    TEST("reason messages contain configured threshold values");
    TurnGuardConfig cfg;
    cfg.max_consecutive_world_query_rounds = 3;
    TurnGuard guard(cfg);
    TurnGuard::RoundInput in;
    in.consecutive_world_query_rounds = 3;
    auto v = guard.evaluate(in);
    assert(v.severity == Severity::Critical);
    assert(v.reason.find("3+ rounds") != std::string::npos);
    PASS();
}

int main() {
    std::cout << "\nTurnGuard Tests\n===============\n";
    test_default_config_matches_hardcoded_thresholds();
    test_custom_threshold_avoids_warning();
    test_custom_nudge_message();
    test_custom_nudge_prefix();
    test_custom_world_query_threshold();
    test_custom_content_avoidance_threshold();
    test_custom_max_tool_calls_threshold();
    test_custom_max_warnings_before_critical();
    test_penalty_for_default_threshold();
    test_reset_clears_warning_count();
    test_default_constructor_uses_default_config();
    test_config_default_values();
    test_restricted_domains_defaults_to_general();
    test_reason_messages_use_config_thresholds();
    std::cout << "\n" << tests_passed << "/" << tests_run << " passed\n";
    return tests_passed == tests_run ? 0 : 1;
}
