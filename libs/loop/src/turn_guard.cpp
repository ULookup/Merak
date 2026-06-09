#include <merak/turn_guard.hpp>

namespace merak {

int TurnGuard::penalty_for(int count) const {
  if (count >= 4) return -999;
  return -(2 * count);
}

TurnGuard::Verdict TurnGuard::evaluate(const RoundInput& in) {
  Verdict v;

  if (in.stall.level == StallLevel::ForceStop) {
    v.severity = Severity::Critical;
    v.reason = "force_stop: 5 consecutive identical tool-call rounds";
    return v;
  }

  if (in.consecutive_world_query_rounds >= 5) {
    v.severity = Severity::Critical;
    v.reason = "5+ rounds of world-only queries without narrative output";
    v.restricted_tools = {"query_map", "query_world", "query_history", "query_magic", "query_faction"};
    v.turn_penalty = -4;
    return v;
  }

  if (in.consecutive_read_only_rounds >= 3) {
    v.severity = Severity::Warning;
    v.reason = "3+ rounds without write operations";
    v.nudge = "你已经观察了很多信息，现在是时候写内容了。";
  }

  if (in.consecutive_content_avoidance >= 3) {
    v.severity = Severity::Warning;
    v.reason = "3x refusal to advance narrative";
    v.nudge = "接受不完美，先写下来，后面可以改。";
  }

  if (in.tool_count >= 15) {
    v.severity = Severity::Warning;
    v.reason = "excessive tool calls in single round";
    v.turn_penalty = -2;
  }

  if (in.had_duplicate_creation) {
    if (v.severity < Severity::Warning) v.severity = Severity::Warning;
    v.nudge = "检查是否已存在同名角色或地点。";
  }

  if (in.had_tone_drift) {
    if (v.severity < Severity::Info) v.severity = Severity::Info;
    v.nudge = "留意你的叙事语气，保持与场景时代背景一致。";
  }

  if (in.stall.level == StallLevel::SigStall) {
    if (v.severity < Severity::Warning) v.severity = Severity::Warning;
    if (!v.nudge) v.nudge = "试着调用 write_file 把想法写出来。";
  }

  if (v.severity >= Severity::Warning) {
    warning_count_++;
    if (!v.turn_penalty) {
      v.turn_penalty = penalty_for(warning_count_);
    }
    if (warning_count_ >= 4) {
      v.severity = Severity::Critical;
      v.reason = "4+ warnings in this run";
    }
  }

  return v;
}

void TurnGuard::reset() {
  warning_count_ = 0;
}

} // namespace merak
