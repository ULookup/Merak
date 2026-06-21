#include <merak/turn_guard.hpp>

namespace merak {

int TurnGuard::penalty_for(int count) const {
  if (count >= config_.max_warnings_before_critical) return -999;
  return -(2 * count);
}

TurnGuard::Verdict TurnGuard::evaluate(const RoundInput& in) {
  std::lock_guard<std::mutex> lock(mutex_);
  Verdict v;

  if (in.stall.level == StallLevel::ForceStop) {
    v.severity = Severity::Critical;
    v.reason = "force_stop: 5 consecutive identical tool-call rounds";
    return v;
  }

  if (in.consecutive_world_query_rounds >= config_.max_consecutive_world_query_rounds) {
    v.severity = Severity::Critical;
    v.reason = "5+ rounds of world-only queries without narrative output";
    v.restricted_tools = {"query_map", "query_world", "query_history", "query_magic", "query_faction"};
    v.turn_penalty = -4;
    return v;
  }

  if (in.consecutive_read_only_rounds >= config_.max_consecutive_read_only_rounds) {
    v.severity = Severity::Warning;
    v.reason = "3+ rounds without write operations";
    v.nudge = config_.nudge_prefix + config_.nudge_write_now;
  }

  if (in.consecutive_content_avoidance >= config_.max_consecutive_content_avoidance) {
    v.severity = Severity::Warning;
    v.reason = "3x refusal to advance narrative";
    v.nudge = config_.nudge_prefix + config_.nudge_accept_imperfection;
  }

  if (in.tool_count >= config_.max_tool_calls_per_round) {
    v.severity = Severity::Warning;
    v.reason = "excessive tool calls in single round";
    v.turn_penalty = -2;
  }

  if (in.had_duplicate_creation) {
    if (v.severity < Severity::Warning) v.severity = Severity::Warning;
    v.nudge = config_.nudge_prefix + config_.nudge_check_duplicates;
  }

  if (in.had_tone_drift) {
    if (v.severity < Severity::Info) v.severity = Severity::Info;
    v.nudge = config_.nudge_prefix + config_.nudge_tone_consistency;
  }

  if (in.stall.level == StallLevel::SigStall) {
    if (v.severity < Severity::Warning) v.severity = Severity::Warning;
    if (!v.nudge) v.nudge = config_.nudge_prefix + config_.nudge_try_write_tool;
  }

  if (v.severity >= Severity::Warning) {
    warning_count_++;
    if (!v.turn_penalty) {
      v.turn_penalty = penalty_for(warning_count_);
    }
    if (warning_count_ >= config_.max_warnings_before_critical) {
      v.severity = Severity::Critical;
      v.reason = "4+ warnings in this run";
    }
  }

  return v;
}

void TurnGuard::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  warning_count_ = 0;
}

} // namespace merak
