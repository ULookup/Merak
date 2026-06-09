#pragma once
#include <merak/section_kind.hpp>
#include <merak/pipeline_types.hpp>
#include <memory>

namespace merak {

class PipelineStats;

struct PlanOutput {
  CompactionTier tier;
  SectionManifest manifest;
  OptimizeLimits limits;
};

class ContextPlanner {
public:
  ContextPlanner() = default;

  PlanOutput plan(const PlanInput& input, const PipelineStats& stats) const;

private:
  static CompactionTier select_tier(double raw_pressure, double predictive_pressure,
                                     bool prev_context_window_error);
  static SectionManifest build_manifest(int effective_budget, CompactionTier tier,
                                         int schema_count, double avg_schema_tokens,
                                         const PipelineStats& stats);
  static OptimizeLimits build_limits(CompactionTier tier);
};

} // namespace merak
