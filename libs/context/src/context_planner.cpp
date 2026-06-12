#include <merak/context_planner.hpp>
#include <merak/pipeline_stats.hpp>
#include <algorithm>

namespace merak {

static constexpr int FIXED_SECTION_CAP = 2000;
static constexpr int WORKING_MEMORY_FLOOR = 500;
static constexpr double OUTPUT_RESERVE_FLOOR = 4096.0;
static constexpr double NORMAL_MEMORY_RATIO = 0.15;
static constexpr double COMPACT_MEMORY_RATIO = 0.10;
static constexpr double AGGRESSIVE_MEMORY_RATIO = 0.05;

CompactionTier ContextPlanner::select_tier(double raw, double predictive,
                                            bool prev_error) {
  CompactionTier base;
  if (raw < 0.60) base = CompactionTier::Normal;
  else if (raw < 0.75) base = CompactionTier::TrimSchemas;
  else if (raw < 0.90) base = CompactionTier::CompactHistory;
  else base = CompactionTier::AggressivePrune;

  CompactionTier pred;
  if (predictive < 0.60) pred = CompactionTier::Normal;
  else if (predictive < 0.75) pred = CompactionTier::TrimSchemas;
  else if (predictive < 0.90) pred = CompactionTier::CompactHistory;
  else pred = CompactionTier::AggressivePrune;

  auto tier = base >= pred ? base : pred;

  if (prev_error) {
    if (tier == CompactionTier::Normal) tier = CompactionTier::TrimSchemas;
    else if (tier == CompactionTier::TrimSchemas) tier = CompactionTier::CompactHistory;
    else tier = CompactionTier::AggressivePrune;
  }

  return tier;
}

SectionManifest ContextPlanner::build_manifest(int effective_budget, CompactionTier tier,
                                                int schema_count, double avg_schema_tokens,
                                                const PipelineStats& stats) {
  SectionManifest m;
  m.sections.push_back({SectionKind::Identity, CacheScope::Global, std::min(FIXED_SECTION_CAP, effective_budget / 10)});
  m.sections.push_back({SectionKind::Constraints, CacheScope::Global, std::min(FIXED_SECTION_CAP, effective_budget / 10)});

  int used = m.sections[0].token_budget + m.sections[1].token_budget;
  int remaining = std::max(0, effective_budget - used);

  int world_budget = std::min(remaining / 4, static_cast<int>(stats.section_usage_ema(SectionKind::WorldContext) * 1.5));
  if (world_budget < 200) world_budget = std::min(200, remaining / 8);
  m.sections.push_back({SectionKind::WorldContext, CacheScope::Session, world_budget});

  int skills_budget = std::min(remaining / 8, static_cast<int>(stats.section_usage_ema(SectionKind::Skills) * 1.5));
  if (skills_budget < 100) skills_budget = 0;
  m.sections.push_back({SectionKind::Skills, CacheScope::Session, skills_budget});

  int schema_budget = static_cast<int>(schema_count * avg_schema_tokens);
  schema_budget = std::min(schema_budget, remaining / 3);
  m.sections.push_back({SectionKind::ToolSchemas, CacheScope::Session, schema_budget});

  used += world_budget + skills_budget + schema_budget;
  remaining = std::max(0, effective_budget - used);

  int wm_budget = std::min(remaining / 5, static_cast<int>(stats.section_usage_ema(SectionKind::WorkingMemory) * 1.5));
  wm_budget = std::max(wm_budget, std::min(WORKING_MEMORY_FLOOR, remaining));
  m.sections.push_back({SectionKind::WorkingMemory, CacheScope::Turn, wm_budget});

  double mem_ratio = tier >= CompactionTier::AggressivePrune ? AGGRESSIVE_MEMORY_RATIO
                   : tier >= CompactionTier::CompactHistory ? COMPACT_MEMORY_RATIO
                   : NORMAL_MEMORY_RATIO;
  int mem_budget = static_cast<int>(remaining * mem_ratio);
  m.sections.push_back({SectionKind::Memory, CacheScope::Turn, mem_budget});

  used += wm_budget + mem_budget;
  remaining = std::max(0, effective_budget - used);

  m.sections.push_back({SectionKind::Conversation, CacheScope::Turn, remaining});

  m.total_budget = effective_budget;
  return m;
}

OptimizeLimits ContextPlanner::build_limits(CompactionTier tier) {
  OptimizeLimits lim;
  lim.allow_reorder = tier >= CompactionTier::Normal;
  lim.allow_schema_pruning = tier >= CompactionTier::TrimSchemas;
  lim.allow_tool_result_clearing = tier >= CompactionTier::CompactHistory;
  lim.allow_round_dropping = tier >= CompactionTier::AggressivePrune;
  lim.allow_spill = tier >= CompactionTier::AggressivePrune;
  return lim;
}

PlanOutput ContextPlanner::plan(const PlanInput& in, const PipelineStats& stats) const {
  int model_max = std::max(1, in.model_max);
  double raw_pressure = static_cast<double>(in.current_tokens) / model_max;
  double output_reserve = std::max(OUTPUT_RESERVE_FLOOR, stats.response_tokens_p50());
  double thinking_reserve = std::max(0.0, stats.thinking_tokens_p50());
  double schema_reserve = in.schema_count * in.avg_schema_tokens;
  double predictive_pressure = (in.current_tokens + output_reserve + thinking_reserve + schema_reserve) / model_max;
  auto tier = select_tier(raw_pressure, predictive_pressure, stats.last_turn_context_window_error());
  if (tier == CompactionTier::AggressivePrune && in.on_escalate) {
    in.on_escalate();
  }
  double reserve_ratio = (output_reserve + thinking_reserve) / model_max;
  int effective_budget = static_cast<int>(model_max * (1.0 - std::max(0.10, reserve_ratio)));
  auto manifest = build_manifest(effective_budget, tier, in.schema_count, in.avg_schema_tokens, stats);
  auto limits = build_limits(tier);
  return {tier, std::move(manifest), limits};
}

} // namespace merak
