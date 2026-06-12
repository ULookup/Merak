#pragma once
#include <merak/section_kind.hpp>
#include <merak/pipeline_types.hpp>
#include <map>
#include <deque>

namespace merak {

struct EmaEstimate {
  double value = 0.0;
  int sample_count = 0;
  void update(double observed, double alpha = 0.2);
};

struct PercentileEstimate {
  std::deque<double> samples;
  int max_samples = 50;
  void record(double v);
  double percentile(double p) const;
};

class PipelineStats {
public:
  void record(const ContextFeedback& feedback, const OptimizeStats& opt_stats);
  void record_cache_hit(bool hit) {
    if (hit) cache_hits_++;
    cache_checks_++;
  }

  double response_tokens_p50() const { return response_tokens_.percentile(0.5); }
  double response_tokens_p90() const { return response_tokens_.percentile(0.9); }
  double thinking_tokens_p50() const { return thinking_tokens_.percentile(0.5); }
  double cache_hit_ratio() const { return cache_hit_ratio_.value; }
  bool last_turn_context_window_error() const { return last_context_window_error_; }

  double section_usage_ema(SectionKind kind) const;
  double avg_schema_tokens() const { return avg_schema_tokens_.value; }
  int schema_count() const { return schema_count_; }

  void reset();

private:
  PercentileEstimate response_tokens_;
  PercentileEstimate thinking_tokens_;
  EmaEstimate cache_hit_ratio_;
  EmaEstimate avg_schema_tokens_;
  std::map<SectionKind, EmaEstimate> section_usage_;
  int schema_count_ = 0;
  bool last_context_window_error_ = false;
  int turn_count_ = 0;
  int cache_hits_ = 0;
  int cache_checks_ = 0;
};

} // namespace merak
