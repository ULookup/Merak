#pragma once
#include <merak/section_kind.hpp>
#include <merak/pipeline_types.hpp>
#include <merak/message.hpp>
#include <merak/tool_spec.hpp>
#include <vector>
#include <string>
#include <set>

namespace merak {

class ContextOptimizer {
public:
  ContextOptimizer() = default;

  std::vector<ToolSpec> prune_schemas(const std::vector<ToolSpec>& specs,
                                       CompactionTier tier) const;

  BoundContext reorder(const BoundContext& ctx, const OptimizeLimits& limits) const;

  void microcompact(std::vector<Message>& history, const OptimizeLimits& limits) const;

  static bool is_non_compactable(const std::string& tool_name);
  static std::string truncate_to_first_sentence(const std::string& text);
};

} // namespace merak
