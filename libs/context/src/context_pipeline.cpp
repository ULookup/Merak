#include <merak/context_pipeline.hpp>
#include <merak/token_counter.hpp>
#include <spdlog/spdlog.h>

namespace merak {

ContextPipeline::ContextPipeline()
  : spill_store_(std::filesystem::temp_directory_path() / "merak_spill") {
}

ContextPipeline::ContextPipeline(const std::filesystem::path& spill_dir)
  : spill_store_(spill_dir) {
}

SerializedPayload ContextPipeline::planned_assemble(
    const std::string& system_prompt,
    const std::string& model,
    int model_max_tokens,
    const std::vector<Message>& history,
    const BindSources& sources) {

  std::lock_guard<std::mutex> lock(mutex_);

  TokenCounter counter(model);
  current_tokens_ = counter.count(history) + counter.count(system_prompt);

  int schema_count = static_cast<int>(sources.tool_specs.size());
  double avg_schema = stats_.avg_schema_tokens();
  if (avg_schema <= 0) avg_schema = 100.0;

  PlanInput pin{current_tokens_, model_max_tokens, schema_count, avg_schema};
  pin.on_escalate = [this]() { escalate_for_recovery(); };
  auto plan = planner_.plan(pin, stats_);

  auto bound = binder_.bind(plan.manifest, sources);

  OptimizeStats opt_stats;
  opt_stats.tokens_before = current_tokens_;

  optimizer_.set_compactor(compactor_);

  if (plan.limits.allow_schema_pruning) {
    bound.tool_schemas = optimizer_.prune_schemas(bound.tool_schemas, plan.tier, opt_stats);
  }
  if (plan.limits.allow_reorder) {
    bound = optimizer_.reorder(bound, plan.limits, opt_stats);
  }

  // Drop oldest rounds from the messages that will be serialized
  if (plan.limits.allow_round_dropping) {
    optimizer_.drop_rounds(bound.provider_messages, plan.limits, opt_stats);
  }

  // Microcompact tool results in the messages that will be serialized
  if (plan.limits.allow_tool_result_clearing) {
    optimizer_.microcompact(bound.provider_messages, plan.limits, opt_stats);
  }

  // Spill oversized sections to disk
  if (plan.limits.allow_spill) {
    optimizer_.spill_sections(bound, spill_store_, turn_index_, plan.limits, opt_stats);
  }

  auto split = CacheAwareContext::split(bound.provider_messages);
  spdlog::debug("split: {}", CacheAwareContext::info(split));
  if (prev_split_.has_value()) {
    bool hit = CacheAwareContext::will_cache_hit(*prev_split_, split);
    stats_.record_cache_hit(hit);
  }
  prev_split_ = split;

  // Compute tokens_after from final state (post-drop/microcompact/spill)
  opt_stats.tokens_after = 0;
  for (auto& sec : bound.sections) {
    opt_stats.tokens_after += sec.token_count;
  }
  for (auto& msg : bound.provider_messages) {
    opt_stats.tokens_after += static_cast<int>(msg.content.size() / 3.5);
  }
  opt_stats.tokens_after += static_cast<int>(system_prompt.size() / 3.5);

  // Hard trim: enforce model_max_tokens as hard ceiling.
  // Round-aware: deletes whole rounds (user-led) to preserve tool_use/tool_result
  // pairing. Re-scans round_starts each iteration to avoid index drift.
  // Runs BEFORE serialize() so the trimmed message list is what gets serialized.
  if (opt_stats.tokens_after > model_max_tokens) {
      auto& msgs = bound.provider_messages;
      int removed = 0;
      while (opt_stats.tokens_after > model_max_tokens) {
          std::vector<size_t> rs;
          for (size_t i = 0; i < msgs.size(); i++) {
              if (msgs[i].role == "user") rs.push_back(i);
          }
          if (rs.size() <= 1) break;  // preserve at least one round

          size_t del_end = rs[1];
          int chars = 0;
          for (size_t i = rs[0]; i < del_end; i++) {
              chars += static_cast<int>(msgs[i].content.size());
          }
          opt_stats.tokens_after -= chars / 3.5;
          msgs.erase(msgs.begin() + static_cast<long>(rs[0]),
                     msgs.begin() + static_cast<long>(del_end));
          removed += static_cast<int>(del_end - rs[0]);
      }
      stats_.hard_trims += removed;
      spdlog::warn("ContextPipeline: hard trim removed {} messages (round-aware) "
                   "(tokens_after={}, max={})",
                   removed, opt_stats.tokens_after, model_max_tokens);
  }

  auto payload = serializer_.serialize(bound, model, system_prompt);

  // Record feedback for next-turn planning
  ContextFeedback fb{};
  fb.schema_count = schema_count;
  stats_.record(fb, opt_stats);

  turn_index_++;

  return payload;
}

void ContextPipeline::escalate_for_recovery() {
  spill_store_.purge_before(turn_index_ + 1);
  stats_.record(ContextFeedback{0, 0, 0, 0, 0, true, false, true, 0}, OptimizeStats{});
}

} // namespace merak
