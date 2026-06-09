#include <merak/context_serializer.hpp>
#include <sstream>

namespace merak {

SerializedPayload ContextSerializer::serialize(
    const BoundContext& ctx, const std::string& model,
    const std::string& system_prompt_full, int max_output_tokens) const {

  SerializedPayload payload;
  payload.is_anthropic = model.find("claude") != std::string::npos;

  // Build system text from bound sections
  std::ostringstream system_oss;
  for (auto& sec : ctx.sections) {
    if (sec.kind == SectionKind::Conversation) continue;
    if (!sec.was_bound || sec.content.empty()) continue;
    system_oss << "[" << section_kind_name(sec.kind) << "]\n"
               << sec.content << "\n\n";
  }
  std::string system_text = system_oss.str();
  if (!system_prompt_full.empty()) {
    system_text = system_prompt_full + "\n\n" + system_text;
  }

  payload.messages = ctx.provider_messages;
  payload.tool_schemas = ctx.tool_schemas;

  // ── OpenAI format ──────────────────────────────────────────────
  payload.openai_json["model"] = model;
  payload.openai_json["messages"] = nlohmann::json::array();

  nlohmann::json sys_msg;
  sys_msg["role"] = "system";
  sys_msg["content"] = system_text;
  payload.openai_json["messages"].push_back(sys_msg);

  for (auto& msg : payload.messages) {
    nlohmann::json j;
    j["role"] = msg.role;
    j["content"] = msg.content;
    if (!msg.tool_calls.empty()) {
      j["tool_calls"] = nlohmann::json::array();
      for (auto& tc : msg.tool_calls) {
        nlohmann::json tcj;
        tcj["id"] = tc.id;
        tcj["type"] = "function";
        tcj["function"]["name"] = tc.name;
        tcj["function"]["arguments"] = tc.arguments;
        j["tool_calls"].push_back(tcj);
      }
    }
    if (msg.tool_call_id) {
      j["tool_call_id"] = *msg.tool_call_id;
    }
    payload.openai_json["messages"].push_back(j);
  }

  payload.openai_json["tools"] = nlohmann::json::array();
  for (auto& ts : payload.tool_schemas) {
    nlohmann::json tj;
    tj["type"] = "function";
    tj["function"]["name"] = ts.name;
    tj["function"]["description"] = ts.description;
    try {
      tj["function"]["parameters"] = nlohmann::json::parse(ts.parameters_json);
    } catch (...) {
      tj["function"]["parameters"] = nlohmann::json::object();
    }
    payload.openai_json["tools"].push_back(tj);
  }

  // ── Anthropic format ───────────────────────────────────────────
  if (payload.is_anthropic) {
    payload.anthropic_json["model"] = model;
    payload.anthropic_json["max_tokens"] = max_output_tokens;
    payload.anthropic_json["system"] = system_text;

    payload.anthropic_json["messages"] = nlohmann::json::array();

    // Convert messages to Anthropic content-block format.
    // Consecutive tool-result messages are merged into a single user message
    // with multiple tool_result content blocks (Anthropic convention).
    for (size_t i = 0; i < payload.messages.size(); i++) {
      auto& msg = payload.messages[i];
      nlohmann::json anth_msg;

      if (msg.role == "tool") {
        // Tool result → user message with tool_result content block.
        // Merge consecutive tool results into one user message.
        anth_msg["role"] = "user";
        anth_msg["content"] = nlohmann::json::array();

        while (i < payload.messages.size() && payload.messages[i].role == "tool") {
          auto& tool_msg = payload.messages[i];
          nlohmann::json tr_block;
          tr_block["type"] = "tool_result";
          if (tool_msg.tool_call_id) {
            tr_block["tool_use_id"] = *tool_msg.tool_call_id;
          }
          tr_block["content"] = tool_msg.content;
          anth_msg["content"].push_back(tr_block);
          i++;
        }
        i--; // outer loop will advance
        payload.anthropic_json["messages"].push_back(anth_msg);
        continue;
      }

      anth_msg["role"] = msg.role;

      bool has_tool_calls = !msg.tool_calls.empty();
      bool has_text = !msg.content.empty();

      if (has_tool_calls) {
        // Assistant with tool use(s) → content blocks array
        anth_msg["content"] = nlohmann::json::array();

        if (has_text) {
          nlohmann::json text_block;
          text_block["type"] = "text";
          text_block["text"] = msg.content;
          anth_msg["content"].push_back(text_block);
        }

        for (auto& tc : msg.tool_calls) {
          nlohmann::json tu_block;
          tu_block["type"] = "tool_use";
          tu_block["id"] = tc.id;
          tu_block["name"] = tc.name;
          try {
            tu_block["input"] = nlohmann::json::parse(tc.arguments);
          } catch (...) {
            tu_block["input"] = nlohmann::json::object();
          }
          anth_msg["content"].push_back(tu_block);
        }
      } else {
        // Pure text message — use string content for user, array for assistant
        if (msg.role == "user") {
          anth_msg["content"] = msg.content;
        } else {
          anth_msg["content"] = nlohmann::json::array();
          nlohmann::json text_block;
          text_block["type"] = "text";
          text_block["text"] = msg.content;
          anth_msg["content"].push_back(text_block);
        }
      }

      payload.anthropic_json["messages"].push_back(anth_msg);
    }

    // Anthropic tools format
    payload.anthropic_json["tools"] = nlohmann::json::array();
    for (auto& ts : payload.tool_schemas) {
      nlohmann::json at;
      at["name"] = ts.name;
      at["description"] = ts.description;
      try {
        at["input_schema"] = nlohmann::json::parse(ts.parameters_json);
      } catch (...) {
        at["input_schema"] = nlohmann::json::object();
      }
      payload.anthropic_json["tools"].push_back(at);
    }
  }

  return payload;
}

} // namespace merak
