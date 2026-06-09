#include <merak/turn_ingestor.hpp>
#include <algorithm>

namespace merak {

IngestedTurn TurnIngestor::ingest(const ToolCall* tool_calls, size_t tool_count,
                                    const TokenCount& tokens,
                                    const std::string& first_text_line,
                                    std::chrono::milliseconds latency,
                                    int turn_index) {
  IngestedTurn t;
  t.index = turn_index;
  t.tokens = tokens;
  t.first_text_line = first_text_line.substr(0, 200);
  t.llm_latency = latency;
  t.had_error = false;

  t.tool_count = static_cast<int>(tool_count);
  for (size_t i = 0; i < tool_count; i++) {
    auto sigs = StallDetector::signatures_of({tool_calls[i]});
    if (!sigs.empty()) t.tool_sigs.push_back(sigs[0]);
  }

  return t;
}

LlmErrorClass TurnIngestor::classify_error(int http_status,
                                             const std::string& body_hint) {
  if (http_status == 401 || http_status == 403) return LlmErrorClass::Auth;
  if (http_status == 429) return LlmErrorClass::RateLimit;
  if (http_status == 400 &&
      (body_hint.find("token") != std::string::npos ||
       body_hint.find("context") != std::string::npos ||
       body_hint.find("length") != std::string::npos)) {
    return LlmErrorClass::ContextWindow;
  }
  if (http_status >= 500) return LlmErrorClass::Unknown;
  return LlmErrorClass::Unknown;
}

void TurnIngestor::set_tool_output_chars(IngestedTurn& turn, int total_chars) {
  turn.total_tool_output_chars = total_chars;
}

} // namespace merak
