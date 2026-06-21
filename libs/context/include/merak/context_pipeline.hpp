#pragma once
#include <merak/cache_aware_context.hpp>
#include <merak/context_planner.hpp>
#include <merak/context_binder.hpp>
#include <merak/context_optimizer.hpp>
#include <merak/context_serializer.hpp>
#include <merak/pipeline_stats.hpp>
#include <merak/spill_store.hpp>
#include <memory>
#include <filesystem>
#include <optional>
#include <mutex>

namespace merak {

class Compactor;

class ContextPipeline {
public:
  ContextPipeline();
  explicit ContextPipeline(const std::filesystem::path& spill_dir);

  void set_compactor(std::shared_ptr<Compactor> compactor) { compactor_ = compactor; }

  SerializedPayload planned_assemble(const std::string& system_prompt,
                                      const std::string& model,
                                      int model_max_tokens,
                                      const std::vector<Message>& history,
                                      const BindSources& sources);

  PipelineStats& stats() { return stats_; }
  const PipelineStats& stats() const { return stats_; }
  void escalate_for_recovery();

  ContextPlanner& planner() { return planner_; }
  ContextOptimizer& optimizer() { return optimizer_; }

private:
  ContextPlanner planner_;
  ContextBinder binder_;
  ContextOptimizer optimizer_;
  ContextSerializer serializer_;
  PipelineStats stats_;
  SpillStore spill_store_;
  std::optional<CacheAwareContext::Split> prev_split_;
  std::shared_ptr<Compactor> compactor_;
  mutable std::mutex mutex_;
  int current_tokens_ = 0;
  int turn_index_ = 0;
};

} // namespace merak
