#pragma once
#include <merak/section_kind.hpp>
#include <merak/pipeline_types.hpp>
#include <merak/message.hpp>
#include <merak/tool_spec.hpp>
#include <vector>
#include <string>
#include <set>
#include <memory>

namespace merak {

class Compactor;

class ContextOptimizer {
public:
  ContextOptimizer() = default;

  void set_compactor(std::shared_ptr<Compactor> compactor) { compactor_ = compactor; }

  std::vector<ToolSpec> prune_schemas(const std::vector<ToolSpec>& specs,
                                       CompactionTier tier,
                                       OptimizeStats& stats) const;

  BoundContext reorder(const BoundContext& ctx,
                       const OptimizeLimits& limits,
                       OptimizeStats& stats) const;

  void microcompact(std::vector<Message>& history,
                    const OptimizeLimits& limits,
                    OptimizeStats& stats) const;

  // Drop oldest rounds to stay under budget. Only when allow_round_dropping is set.
  void drop_rounds(std::vector<Message>& history,
                   const OptimizeLimits& limits,
                   OptimizeStats& stats) const;

  // Spill oversized sections to disk store. Only when allow_spill is set.
  void spill_sections(BoundContext& ctx,
                      class SpillStore& store,
                      int turn_index,
                      const OptimizeLimits& limits,
                      OptimizeStats& stats) const;

  static bool is_non_compactable(const std::string& tool_name);
  static std::string truncate_to_first_sentence(const std::string& text);

private:
  std::shared_ptr<Compactor> compactor_;
};

} // namespace merak
