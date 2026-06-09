#pragma once
#include <merak/section_kind.hpp>
#include <merak/message.hpp>
#include <merak/stall_detector.hpp>
#include <string>
#include <vector>
#include <optional>

namespace merak {

class TurnGuard {
public:
  struct Verdict {
    Severity severity = Severity::Healthy;
    std::string reason;
    std::optional<std::string> nudge;
    std::optional<int> turn_penalty;
    std::vector<std::string> restricted_tools;
  };

  struct RoundInput {
    int turn_index = 0;
    int tool_count = 0;
    bool had_write_operation = false;
    int consecutive_read_only_rounds = 0;
    int consecutive_world_query_rounds = 0;
    int consecutive_content_avoidance = 0;
    bool had_duplicate_creation = false;
    bool had_tone_drift = false;
    StallResult stall;
  };

  TurnGuard() = default;

  Verdict evaluate(const RoundInput& input);

  void reset();

  int warning_count() const { return warning_count_; }

private:
  int warning_count_ = 0;

  int penalty_for(int count) const;
};

} // namespace merak
