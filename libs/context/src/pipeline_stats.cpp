#include <merak/pipeline_stats.hpp>
#include <algorithm>
#include <cmath>

namespace merak {

void EmaEstimate::update(double observed, double alpha) {
  if (sample_count == 0) {
    value = observed;
  } else {
    value = alpha * observed + (1.0 - alpha) * value;
  }
  sample_count++;
}

void PercentileEstimate::record(double v) {
  samples.push_back(v);
  if (static_cast<int>(samples.size()) > max_samples) samples.pop_front();
}

double PercentileEstimate::percentile(double p) const {
  if (samples.empty()) return 0.0;
  auto sorted = samples;
  std::sort(sorted.begin(), sorted.end());
  auto idx = static_cast<size_t>(p * static_cast<double>(sorted.size() - 1));
  if (idx >= sorted.size()) idx = sorted.size() - 1;
  return sorted[idx];
}

void PipelineStats::record(const ContextFeedback& feedback, const OptimizeStats& opt_stats) {
  response_tokens_.record(static_cast<double>(feedback.output_tokens));
  if (feedback.thinking_tokens > 0) {
    thinking_tokens_.record(static_cast<double>(feedback.thinking_tokens));
  }
  double hit = feedback.input_tokens > 0
    ? static_cast<double>(feedback.cache_read_tokens) / static_cast<double>(feedback.input_tokens)
    : 0.0;
  cache_hit_ratio_.update(hit);

  if (feedback.input_tokens > 0 && feedback.schema_count > 0) {
    avg_schema_tokens_.update(static_cast<double>(feedback.schema_count) / static_cast<double>(feedback.input_tokens) * 100.0);
  }
  schema_count_ = feedback.schema_count;
  last_context_window_error_ = feedback.context_window_error;
  turn_count_++;

  // Track per-section usage from optimize stats
  for (auto& action : opt_stats.actions) {
    // Accumulate section usage data from optimizer actions
  }
}

double PipelineStats::section_usage_ema(SectionKind kind) const {
  auto it = section_usage_.find(kind);
  return it != section_usage_.end() ? it->second.value : 0.0;
}

void PipelineStats::reset() {
  *this = PipelineStats{};
}

} // namespace merak
