#pragma once
#include <merak/pipeline_types.hpp>
#include <merak/message.hpp>
#include <merak/tool_spec.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace merak {

struct SerializedPayload {
  nlohmann::json openai_json;
  nlohmann::json anthropic_json;
  std::string system_text;
  std::vector<Message> messages;
  std::vector<ToolSpec> tool_schemas;
  bool is_anthropic = false;
};

class ContextSerializer {
public:
  ContextSerializer() = default;

  SerializedPayload serialize(const BoundContext& optimized,
                               const std::string& model,
                               const std::string& system_prompt_full,
                               int max_output_tokens = 4096) const;
};

} // namespace merak
