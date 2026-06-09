#pragma once
#include <merak/message.hpp>
#include <merak/execution.hpp>
#include <merak/stall_detector.hpp>
#include <chrono>
#include <vector>
#include <optional>

namespace merak {

struct TokenCount {
  int input = 0;
  int output = 0;
  int cache_read = 0;
  int cache_write = 0;
};

struct IngestedTurn {
  int index;
  std::vector<ToolCallSignature> tool_sigs;
  int tool_count;
  int total_tool_output_chars;
  TokenCount tokens;
  bool had_error;
  std::optional<LlmErrorClass> error_class;
  std::chrono::milliseconds llm_latency;
  std::string first_text_line;
};

class TurnIngestor {
public:
  TurnIngestor() = default;

  IngestedTurn ingest(const ToolCall* tool_calls, size_t tool_count,
                       const TokenCount& tokens,
                       const std::string& first_text_line,
                       std::chrono::milliseconds latency,
                       int turn_index);

  static LlmErrorClass classify_error(int http_status, const std::string& body_hint);
  static void set_tool_output_chars(IngestedTurn& turn, int total_chars);
};

} // namespace merak
