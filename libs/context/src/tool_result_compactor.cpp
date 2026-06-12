#include <merak/tool_result_compactor.hpp>
#include <merak/context_optimizer.hpp>

namespace merak {

int ToolResultCompactor::compact(std::vector<Message>& history, double context_pressure) {
  if (context_pressure <= config_.pressure_threshold) return 0;

  OptimizeLimits lim;
  lim.allow_tool_result_clearing = true;
  lim.keep_recent_tool_results = config_.keep_recent;
  lim.max_result_chars = config_.max_result_chars;

  ContextOptimizer opt;
  OptimizeStats stats;
  opt.microcompact(history, lim, stats);
  return static_cast<int>(stats.actions.size());
}

} // namespace merak
