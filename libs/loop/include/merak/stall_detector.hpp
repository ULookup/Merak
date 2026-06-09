#pragma once
#include <merak/section_kind.hpp>
#include <merak/message.hpp>
#include <deque>
#include <vector>
#include <cstddef>
#include <string>

namespace merak {

struct ToolCallSignature {
  std::string tool_name;
  std::size_t args_hash;

  bool operator==(const ToolCallSignature& o) const {
    return tool_name == o.tool_name && args_hash == o.args_hash;
  }
};

struct StallResult {
  bool is_stalled;
  int consecutive_identical;
  ToolCallSignature stalled_signature;
  StallLevel level;
};

class StallDetector {
public:
  struct Config {
    int sig_stall_threshold = 3;
    int force_stop_threshold = 5;
    int max_lookback_rounds = 8;
    Config() = default;
  };

  StallDetector() : config_{} {}
  explicit StallDetector(Config cfg) : config_(cfg) {}

  StallResult check(const std::vector<ToolCall>& current_round);

  void reset();

  static std::vector<ToolCallSignature> signatures_of(const std::vector<ToolCall>& calls);

private:
  struct RoundRecord {
    int turn_index;
    std::vector<ToolCallSignature> signatures;
  };

  Config config_;
  std::deque<RoundRecord> recent_rounds_;
  int turn_counter_ = 0;

  static bool rounds_match(const std::vector<ToolCallSignature>& a,
                            const std::vector<ToolCallSignature>& b);
};

} // namespace merak
