#include <merak/context_optimizer.hpp>
#include <merak/compactor.hpp>
#include <merak/spill_store.hpp>
#include <algorithm>
#include <set>
#include <future>
#include <spdlog/spdlog.h>

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
    const std::vector<ToolSpec>& specs, CompactionTier tier,
    OptimizeStats& stats) const {
  if (tier < CompactionTier::TrimSchemas) return specs;

  auto pruned = specs;
  int chars_saved = 0;
  for (auto& s : pruned) {
    int orig_len = static_cast<int>(s.description.size());
    if (tier >= CompactionTier::AggressivePrune) {
      s.description.clear();
    } else {
      s.description = truncate_to_first_sentence(s.description);
    }
    chars_saved += orig_len - static_cast<int>(s.description.size());
  }
  int tokens_saved = chars_saved / 4;
  stats.tokens_saved += tokens_saved;
  stats.actions.push_back(OptimizerAction{
    "pruned tool schema descriptions", tokens_saved,
    SectionKind::ToolSchemas
  });
  return pruned;
}

BoundContext ContextOptimizer::reorder(const BoundContext& ctx,
                                        const OptimizeLimits& limits,
                                        OptimizeStats& stats) const {
  if (!limits.allow_reorder) return ctx;

  auto result = ctx;

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

  stats.actions.push_back(OptimizerAction{
    "reordered sections by cache scope", 0, std::nullopt
  });
  return result;
}

void ContextOptimizer::microcompact(std::vector<Message>& history,
                                     const OptimizeLimits& limits,
                                     OptimizeStats& stats) const {
  if (!limits.allow_tool_result_clearing) return;

  int tool_msg_count = 0;
  int chars_saved = 0;
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
      chars_saved += static_cast<int>(original_size - it->content.size());
    }
  }
  int tokens_saved = chars_saved / 4;
  stats.tokens_saved += tokens_saved;
  stats.actions.push_back(OptimizerAction{
    "microcompact: truncated old tool results", tokens_saved,
    SectionKind::Conversation
  });
}

void ContextOptimizer::drop_rounds(std::vector<Message>& history,
                                   const OptimizeLimits& limits,
                                   OptimizeStats& stats) const {
  if (!limits.allow_round_dropping) return;

  // Count complete user→assistant→(tool)* round-trips
  std::vector<size_t> round_starts;
  for (size_t i = 0; i < history.size(); i++) {
    if (history[i].role == "user") {
      round_starts.push_back(i);
    }
  }

  int total_rounds = static_cast<int>(round_starts.size());
  int min_keep = limits.min_rounds_to_keep;
  if (min_keep < 1) min_keep = 1;
  int drop_count = total_rounds - min_keep;
  if (drop_count <= 0) return;

  // Remove messages of the oldest N rounds
  // round_starts[drop_count] is the first round we keep
  size_t keep_from = round_starts[static_cast<size_t>(drop_count)];
  int dropped_chars = 0;
  for (size_t i = 0; i < keep_from; i++) {
    dropped_chars += static_cast<int>(history[i].content.size());
  }

  if (compactor_ && drop_count > 0) {
    std::vector<std::future<std::string>> futures;
    for (size_t i = 0; i < static_cast<size_t>(drop_count); i++) {
      size_t start = round_starts[i];
      size_t end = (i + 1 < round_starts.size()) ? round_starts[i + 1] : keep_from;
      std::vector<Message> round_msgs(history.begin() + start, history.begin() + end);
      if (!round_msgs.empty()) {
        futures.push_back(compactor_->compact_one_round(round_msgs));
      }
    }
    // Collect summaries
    std::vector<Message> summaries;
    for (size_t i = 0; i < futures.size(); i++) {
      auto summary = futures[i].get();
      if (!summary.empty()) {
        summaries.push_back({"system", "[Compacted round " + std::to_string(i + 1) + "]: " + summary, {}, "", ""});
      }
    }
    history.erase(history.begin(), history.begin() + static_cast<long>(keep_from));
    history.insert(history.begin(), summaries.begin(), summaries.end());
  } else {
    history.erase(history.begin(), history.begin() + static_cast<long>(keep_from));
  }

  int tokens_saved = dropped_chars / 4;
  stats.tokens_saved += tokens_saved;
  stats.actions.push_back(OptimizerAction{
    "dropped " + std::to_string(drop_count) + " oldest rounds", tokens_saved,
    SectionKind::Conversation
  });

  spdlog::debug("Optimizer: dropped {} rounds, saved ~{} tokens", drop_count, tokens_saved);
}

void ContextOptimizer::spill_sections(BoundContext& ctx,
                                      SpillStore& store,
                                      int turn_index,
                                      const OptimizeLimits& limits,
                                      OptimizeStats& stats) const {
  if (!limits.allow_spill) return;

  for (auto& section : ctx.sections) {
    if (section.kind == SectionKind::Identity ||
        section.kind == SectionKind::Constraints ||
        section.kind == SectionKind::WorkingMemory) {
      continue;
    }
    if (section.content.empty()) continue;

    auto ref = store.spill(section.kind, section.content, turn_index);
    if (!ref.has_value()) {
      spdlog::warn("Optimizer: failed to spill section {}", section_kind_name(section.kind));
      continue;
    }

    int tokens_saved = static_cast<int>(section.content.size()) / 4;
    section.content = "[spilled: " + ref->path + "]";
    section.token_count = static_cast<int>(section.content.size() / 3.5);

    stats.tokens_saved += tokens_saved;
    stats.actions.push_back(OptimizerAction{
      "spilled section " + std::string(section_kind_name(section.kind)),
      tokens_saved,
      section.kind
    });
    spdlog::debug("Optimizer: spilled {} section to {}", section_kind_name(section.kind), ref->path);
  }
}

} // namespace merak
