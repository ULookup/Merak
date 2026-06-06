#pragma once
#include <string>
#include <variant>
#include <vector>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <map>

namespace merak::tui::persistence {

struct UserEvent {
    std::string text;
    uint64_t timestamp_ms = 0;
    std::map<std::string, std::string> paste_refs;
};

struct AssistantEvent {
    std::string text;
    std::string model;
    uint64_t timestamp_ms = 0;
    uint8_t frozen_phase = 0;
};

struct ReasoningEvent {
    std::string text;
    uint64_t timestamp_ms = 0;
    bool collapsed = false;
};

struct ToolEvent {
    std::string tool_name;
    nlohmann::json args;
    std::string output;
    int exit_code = 0;
    uint64_t elapsed_ms = 0;
    enum class Status { running, success, error, cancelled };
    Status status = Status::success;
};

struct SystemEvent {
    std::string text;
    enum class Level { info, warn, error };
    Level level = Level::info;
    uint64_t timestamp_ms = 0;
};

struct TurnSummaryEvent {
    uint32_t tokens_in = 0;
    uint32_t tokens_out = 0;
    double cost_usd = 0.0;
    uint64_t elapsed_ms = 0;
    uint32_t tool_count = 0;
};

struct ApprovalEvent {
    std::string tool_name;
    std::string args_summary;
    enum class Decision { approved, denied };
    Decision decision = Decision::approved;
    uint64_t timestamp_ms = 0;
};

struct SessionMeta {
    std::string session_id;
    std::string title;
    uint64_t created_at = 0;
    std::string model;
    std::string provider;
    std::string permission_mode;
    std::string system_prompt_hash;
    std::string cwd;
    uint16_t terminal_w = 0;
    uint16_t terminal_h = 0;
    std::string merak_version;
};

using TurnEvent = std::variant<
    UserEvent, AssistantEvent, ReasoningEvent, ToolEvent,
    SystemEvent, TurnSummaryEvent, ApprovalEvent, SessionMeta
>;

} // namespace merak::tui::persistence
