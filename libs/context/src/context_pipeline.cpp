#include <merak/context_pipeline.hpp>
#include <merak/token_counter.hpp>

namespace merak {

ContextPipeline::ContextPipeline() = default;

SerializedPayload ContextPipeline::planned_assemble(
    const std::string& system_prompt,
    const std::string& model,
    int model_max_tokens,
    const std::vector<Message>& history,
    const BindSources& sources) {

  TokenCounter counter(model);
  current_tokens_ = counter.count(history) + counter.count(system_prompt);

  int schema_count = static_cast<int>(sources.tool_specs.size());
  double avg_schema = stats_.avg_schema_tokens();
  if (avg_schema <= 0) avg_schema = 100.0;

  PlanInput pin{current_tokens_, model_max_tokens, schema_count, avg_schema};
  auto plan = planner_.plan(pin, stats_);

  auto bound = binder_.bind(plan.manifest, sources);

  if (plan.limits.allow_schema_pruning) {
    bound.tool_schemas = optimizer_.prune_schemas(bound.tool_schemas, plan.tier);
  }
  if (plan.limits.allow_reorder) {
    bound = optimizer_.reorder(bound, plan.limits);
  }

  auto payload = serializer_.serialize(bound, model, system_prompt);

  return payload;
}

void ContextPipeline::escalate_for_recovery() {
  stats_.record(ContextFeedback{0, 0, 0, 0, 0, true, false, true, 0}, OptimizeStats{});
}

} // namespace merak
