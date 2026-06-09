#include <merak/stall_detector.hpp>
#include <nlohmann/json.hpp>
#include <functional>

namespace merak {

std::vector<ToolCallSignature> StallDetector::signatures_of(
    const std::vector<ToolCall>& calls) {
  std::vector<ToolCallSignature> sigs;
  for (auto& c : calls) {
    std::size_t h = 0;
    try {
      auto j = nlohmann::json::parse(c.arguments);
      h = std::hash<std::string>{}(j.dump());
    } catch (...) {
      h = std::hash<std::string>{}(c.arguments);
    }
    sigs.push_back({c.name, h});
  }
  return sigs;
}

bool StallDetector::rounds_match(const std::vector<ToolCallSignature>& a,
                                  const std::vector<ToolCallSignature>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++) {
    if (!(a[i] == b[i])) return false;
  }
  return true;
}

StallResult StallDetector::check(const std::vector<ToolCall>& current_round) {
  auto current_sigs = signatures_of(current_round);

  // Count consecutive identical rounds going backward
  int consecutive = 1;
  for (auto it = recent_rounds_.rbegin(); it != recent_rounds_.rend(); ++it) {
    if (rounds_match(it->signatures, current_sigs)) {
      consecutive++;
    } else {
      break;  // Non-consecutive — reset
    }
  }

  StallLevel level = consecutive >= config_.force_stop_threshold ? StallLevel::ForceStop
                   : consecutive >= config_.sig_stall_threshold ? StallLevel::SigStall
                   : StallLevel::None;

  ToolCallSignature stalled_sig;
  if (!current_sigs.empty()) stalled_sig = current_sigs[0];

  // Record this round
  recent_rounds_.push_back({turn_counter_++, current_sigs});
  while (static_cast<int>(recent_rounds_.size()) > config_.max_lookback_rounds) {
    recent_rounds_.pop_front();
  }

  return {level != StallLevel::None, consecutive, stalled_sig, level};
}

void StallDetector::reset() {
  recent_rounds_.clear();
  turn_counter_ = 0;
}

} // namespace merak
