#pragma once
#include <merak/section_kind.hpp>
#include <merak/message.hpp>
#include <merak/stall_detector.hpp>
#include <string>
#include <vector>
#include <optional>
#include <mutex>

namespace merak {

struct TurnGuardConfig {
    int max_consecutive_world_query_rounds = 5;
    int max_consecutive_read_only_rounds = 3;
    int max_consecutive_content_avoidance = 3;
    int max_tool_calls_per_round = 15;
    int max_warnings_before_critical = 4;

    std::string nudge_write_now =
        "You've gathered a lot of information. It's time to start writing content.";
    std::string nudge_accept_imperfection =
        "Accept imperfection — write it down first, you can revise later.";
    std::string nudge_check_duplicates =
        "Check whether a character or location with the same name already exists.";
    std::string nudge_tone_consistency =
        "Mind your narrative tone — keep it consistent with the scene's era and setting.";
    std::string nudge_try_write_tool =
        "Try using your write tool to get your thoughts onto the page.";
    std::string nudge_prefix = "[Nudge] ";
};

class TurnGuard {
public:
  struct Verdict {
    Severity severity = Severity::Healthy;
    std::string reason;
    std::optional<std::string> nudge;
    std::optional<int> turn_penalty;
    ToolDomain restricted_domains = ToolDomain::General;
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

  explicit TurnGuard(TurnGuardConfig cfg = {}) : config_(std::move(cfg)) {}

  Verdict evaluate(const RoundInput& input);

  void reset();

  int warning_count() const { return warning_count_; }

private:
  TurnGuardConfig config_;
  int warning_count_ = 0;
  mutable std::mutex mutex_;

  int penalty_for(int count) const;
};

} // namespace merak
