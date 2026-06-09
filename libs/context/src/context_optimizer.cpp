#include <merak/context_optimizer.hpp>
#include <algorithm>
#include <set>

namespace merak {

static const std::set<std::string> NON_COMPACTABLE_TOOLS = {
  "execute_bash", "write_file", "str_replace", "multi_edit",
  "delete_file", "skill", "delegate"
};

bool ContextOptimizer::is_non_compactable(const std::string& tool_name) {
  return NON_COMPACTABLE_TOOLS.count(tool_name) > 0;
}

std::string ContextOptimizer::truncate_to_first_sentence(const std::string& text) {
  auto pos = text.find('.');
  if (pos != std::string::npos && pos + 1 < text.size()) {
    return text.substr(0, pos + 1);
  }
  return text;
}

std::vector<ToolSpec> ContextOptimizer::prune_schemas(
    const std::vector<ToolSpec>& specs, CompactionTier tier) const {
  if (tier < CompactionTier::TrimSchemas) return specs;

  auto pruned = specs;
  for (auto& s : pruned) {
    if (tier >= CompactionTier::AggressivePrune) {
      s.description.clear();
    } else {
      s.description = truncate_to_first_sentence(s.description);
    }
  }
  return pruned;
}

BoundContext ContextOptimizer::reorder(const BoundContext& ctx,
                                        const OptimizeLimits& limits) const {
  if (!limits.allow_reorder) return ctx;

  auto result = ctx;

  // Define scope ordering: Global(0) < Session(1) < Turn(2)
  auto scope_priority = [](SectionKind k) -> int {
    switch (k) {
      case SectionKind::Identity:
      case SectionKind::Constraints:
        return 0;
      case SectionKind::WorldContext:
      case SectionKind::Skills:
      case SectionKind::ToolSchemas:
        return 1;
      default:
        return 2;
    }
  };

  std::stable_sort(result.sections.begin(), result.sections.end(),
    [&scope_priority](const BoundSection& a, const BoundSection& b) {
      return scope_priority(a.kind) < scope_priority(b.kind);
    });

  return result;
}

void ContextOptimizer::microcompact(std::vector<Message>& history,
                                     const OptimizeLimits& limits) const {
  if (!limits.allow_tool_result_clearing) return;

  int tool_msg_count = 0;
  // Iterate from most recent to oldest
  for (auto it = history.rbegin(); it != history.rend(); ++it) {
    if (it->role != "tool") continue;
    tool_msg_count++;
    if (tool_msg_count <= limits.keep_recent_tool_results) continue;

    if (static_cast<int>(it->content.size()) > limits.max_result_chars) {
      size_t original_size = it->content.size();
      it->content = it->content.substr(0, static_cast<size_t>(limits.max_result_chars))
                  + "\n[result truncated: "
                  + std::to_string(original_size - static_cast<size_t>(limits.max_result_chars))
                  + " bytes]";
    }
  }
}

} // namespace merak
