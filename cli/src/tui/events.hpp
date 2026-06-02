#pragma once
#include <merak/message.hpp>
#include <merak/turn_state.hpp>
#include <future>
#include <memory>
#include <variant>

namespace merak::tui {

struct TextDelta { std::string text; };
struct ToolStarted { ToolCall call; };
struct ToolEnded { ToolResult result; };
struct StateChanged { TurnState state; };
struct Usage { int input_tokens; int output_tokens; bool exact; };
struct ApprovalRequested { ToolCall call; std::shared_ptr<std::promise<bool>> response; };
struct TurnCompleted { AgentResponse response; };
struct TurnFailed { std::string message; };
struct Interrupted {};
struct SubAgentStarted { std::string id; };
struct SubAgentStateChanged { std::string id; TurnState state; };
struct SubAgentStep { std::string id; };
struct SubAgentEnded { std::string id; bool failed; };

using TuiEvent = std::variant<TextDelta, ToolStarted, ToolEnded, StateChanged,
    Usage, ApprovalRequested, TurnCompleted, TurnFailed, Interrupted, SubAgentStarted,
    SubAgentStateChanged, SubAgentStep, SubAgentEnded>;

} // namespace merak::tui
