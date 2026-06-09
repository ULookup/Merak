#include <merak/context_serializer.hpp>
#include <sstream>

namespace merak {

SerializedPayload ContextSerializer::serialize(
    const BoundContext& ctx, const std::string& model,
    const std::string& system_prompt_full) const {

  SerializedPayload payload;
  payload.is_anthropic = model.find("claude") != std::string::npos;

  std::ostringstream system_oss;
  for (auto& sec : ctx.sections) {
    if (sec.kind == SectionKind::Conversation) continue;
    if (!sec.was_bound || sec.content.empty()) continue;
    system_oss << "[" << section_kind_name(sec.kind) << "]\n"
               << sec.content << "\n\n";
  }

  payload.messages = ctx.provider_messages;
  payload.tool_schemas = ctx.tool_schemas;

  payload.openai_json["model"] = model;
  payload.openai_json["messages"] = nlohmann::json::array();

  nlohmann::json sys_msg;
  sys_msg["role"] = "system";
  sys_msg["content"] = system_oss.str();
  if (!system_prompt_full.empty()) {
    sys_msg["content"] = system_prompt_full + "\n\n" + sys_msg["content"].get<std::string>();
  }
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

  return payload;
}

} // namespace merak
