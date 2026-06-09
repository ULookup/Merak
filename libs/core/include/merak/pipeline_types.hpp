#pragma once
#include <merak/section_kind.hpp>
#include <merak/message.hpp>
#include <merak/tool_spec.hpp>
#include <string>
#include <vector>
#include <optional>
#include <cstddef>

namespace merak {

struct PlannedSection {
  SectionKind kind;
  CacheScope scope;
  int token_budget;
};

struct SectionManifest {
  std::vector<PlannedSection> sections;
  int total_budget;
};

struct BoundSection {
  SectionKind kind;
  std::string content;
  int token_count;
  bool was_bound;
};

struct SpillReference {
  SectionKind kind;
  std::string path;
  size_t byte_count;
  std::string content_hash;
};

struct OptimizedSection {
  SectionKind kind;
  std::string content;
  std::optional<SpillReference> spill;
  int token_count;
};

struct OptimizeLimits {
  bool allow_reorder = true;
  bool allow_schema_pruning = false;
  bool allow_tool_result_clearing = false;
  bool allow_round_dropping = false;
  bool allow_spill = false;
  int max_moves = 20;
  int max_clear_tokens = 50000;
  int keep_recent_tool_results = 6;
  int max_result_chars = 8000;
  int min_rounds_to_keep = 4;
};

struct SectionTrace {
  SectionKind kind;
  CacheScope scope;
  int budget_allocated;
  int actual_tokens;
  bool was_spilled;
  std::string content_preview;
  std::string source;
};

struct OptimizerAction {
  std::string description;
  int tokens_saved;
  std::optional<SectionKind> affected_section;
};

struct ContextFeedback {
  int input_tokens;
  int output_tokens;
  int cache_read_tokens;
  int cache_write_tokens;
  int thinking_tokens;
  bool was_truncated;
  bool cache_break_detected;
  bool context_window_error;
  int schema_count;
};

struct OptimizeStats {
  int tokens_before;
  int tokens_after;
  int tokens_saved;
  std::vector<OptimizerAction> actions;
};

struct PlanInput {
  int current_tokens;
  int model_max;
  int schema_count;
  double avg_schema_tokens;
};

struct BoundContext {
  std::vector<BoundSection> sections;
  std::vector<Message> provider_messages;
  std::vector<ToolSpec> tool_schemas;
};

} // namespace merak
