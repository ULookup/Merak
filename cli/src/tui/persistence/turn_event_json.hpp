#pragma once
#include "turn_event.hpp"
#include <nlohmann/json.hpp>

namespace merak::tui::persistence {

inline void to_json(nlohmann::json& j, const UserEvent& e) {
    j = {{"kind", "user"}, {"text", e.text}, {"ts", e.timestamp_ms}, {"paste_refs", e.paste_refs}};
}
inline void to_json(nlohmann::json& j, const AssistantEvent& e) {
    j = {{"kind", "assistant"}, {"text", e.text}, {"model", e.model}, {"ts", e.timestamp_ms}, {"frozen_phase", e.frozen_phase}};
}
inline void to_json(nlohmann::json& j, const ReasoningEvent& e) {
    j = {{"kind", "reasoning"}, {"text", e.text}, {"ts", e.timestamp_ms}, {"collapsed", e.collapsed}};
}
inline void to_json(nlohmann::json& j, const ToolEvent& e) {
    j = {{"kind", "tool"}, {"name", e.tool_name}, {"args", e.args}, {"output", e.output},
        {"exit_code", e.exit_code}, {"elapsed_ms", e.elapsed_ms},
        {"status", e.status == ToolEvent::Status::running ? "running"
            : e.status == ToolEvent::Status::success ? "success"
            : e.status == ToolEvent::Status::error ? "error" : "cancelled"}};
}
inline void to_json(nlohmann::json& j, const SystemEvent& e) {
    j = {{"kind", "system"}, {"text", e.text},
        {"level", e.level == SystemEvent::Level::info ? "info"
            : e.level == SystemEvent::Level::warn ? "warn" : "error"}, {"ts", e.timestamp_ms}};
}
inline void to_json(nlohmann::json& j, const TurnSummaryEvent& e) {
    j = {{"kind", "turn_summary"}, {"tokens_in", e.tokens_in}, {"tokens_out", e.tokens_out},
        {"cost", e.cost_usd}, {"elapsed_ms", e.elapsed_ms}, {"tool_count", e.tool_count}};
}
inline void to_json(nlohmann::json& j, const ApprovalEvent& e) {
    j = {{"kind", "approval"}, {"tool", e.tool_name}, {"summary", e.args_summary},
        {"decision", e.decision == ApprovalEvent::Decision::approved ? "approved" : "denied"},
        {"ts", e.timestamp_ms}};
}
inline void to_json(nlohmann::json& j, const SessionMeta& e) {
    j = {{"kind", "session_meta"}, {"sid", e.session_id}, {"created_at", e.created_at},
        {"model", e.model}, {"provider", e.provider}, {"permission_mode", e.permission_mode},
        {"cwd", e.cwd}, {"term_w", e.terminal_w}, {"term_h", e.terminal_h},
        {"version", e.merak_version}};
}

inline nlohmann::json to_json(const TurnEvent& event) {
    return std::visit([](const auto& e) -> nlohmann::json { return e; }, event);
}

inline TurnEvent from_json(const nlohmann::json& j) {
    auto kind = j.value("kind", "");
    if (kind == "user") return UserEvent{j.value("text", ""), j.value("ts", 0ULL), j.value("paste_refs", std::map<std::string,std::string>{})};
    if (kind == "assistant") return AssistantEvent{j.value("text", ""), j.value("model", ""), j.value("ts", 0ULL), j.value("frozen_phase", uint8_t{0})};
    if (kind == "reasoning") return ReasoningEvent{j.value("text", ""), j.value("ts", 0ULL), j.value("collapsed", false)};
    if (kind == "tool") {
        ToolEvent e; e.tool_name = j.value("name", ""); e.args = j.value("args", nlohmann::json{});
        e.output = j.value("output", ""); e.exit_code = j.value("exit_code", 0);
        e.elapsed_ms = j.value("elapsed_ms", 0ULL);
        auto s = j.value("status", "success");
        e.status = s == "running" ? ToolEvent::Status::running : s == "error" ? ToolEvent::Status::error : s == "cancelled" ? ToolEvent::Status::cancelled : ToolEvent::Status::success;
        return e;
    }
    if (kind == "system") {
        auto lvl = j.value("level", "info");
        return SystemEvent{j.value("text", ""), lvl == "warn" ? SystemEvent::Level::warn : lvl == "error" ? SystemEvent::Level::error : SystemEvent::Level::info, j.value("ts", 0ULL)};
    }
    if (kind == "turn_summary") return TurnSummaryEvent{j.value("tokens_in", 0U), j.value("tokens_out", 0U), j.value("cost", 0.0), j.value("elapsed_ms", 0ULL), j.value("tool_count", 0U)};
    if (kind == "approval") {
        auto d = j.value("decision", "approved");
        return ApprovalEvent{j.value("tool", ""), j.value("summary", ""), d == "denied" ? ApprovalEvent::Decision::denied : ApprovalEvent::Decision::approved, j.value("ts", 0ULL)};
    }
    if (kind == "session_meta") {
        return SessionMeta{j.value("sid", ""), j.value("created_at", 0ULL), j.value("model", ""), j.value("provider", ""), j.value("permission_mode", ""), j.value("system_prompt_hash", ""), j.value("cwd", ""), j.value("term_w", uint16_t{0}), j.value("term_h", uint16_t{0}), j.value("version", "")};
    }
    return UserEvent{};
}

} // namespace merak::tui::persistence
