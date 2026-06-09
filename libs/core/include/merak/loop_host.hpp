#pragma once
#include <merak/message.hpp>
#include <merak/execution.hpp>
#include <merak/section_kind.hpp>
#include <future>
#include <optional>
#include <string>

namespace merak {

// Forward declarations
class ToolCall;
struct ToolResult;

/**
 * Abstract interface for host-specific I/O.
 * Implementations: CliLoopHost (terminal), ServerLoopHost (SSE/HTTP).
 * This allows the same AgentLoop code to run on CLI, server, and future hosts.
 */
class LoopHost {
public:
  virtual ~LoopHost() = default;

  // ---- Output ----
  /// Called for each streaming text delta from the LLM.
  virtual void on_text_delta(const std::string& delta) = 0;

  /// Called when a tool begins execution.
  virtual void on_tool_start(const ToolCall& call) = 0;

  /// Called when a tool completes execution.
  virtual void on_tool_end(const ToolCall& call, const ToolResult& result) = 0;

  /// Called on state machine transitions.
  virtual void on_state_change(TurnState from, TurnState to) = 0;

  /// Called when a TurnGuard verdict is issued.
  virtual void on_verdict(Severity severity, const std::string& reason,
                           const std::optional<std::string>& nudge) = 0;

  /// Called when stall is detected.
  virtual void on_stall(StallLevel level, int consecutive_rounds) = 0;

  /// Called on non-fatal errors.
  virtual void on_error(const std::string& error_msg) = 0;

  // ---- Input ----
  /// Returns the next user message, or nullopt on EOF/session end.
  virtual std::future<std::optional<Message>> next_user_message() = 0;

  // ---- Approval ----
  /// Blocks until the user approves or denies a tool call.
  virtual std::future<bool> await_approval(const ToolCall& call) = 0;

  // ---- Cancellation ----
  /// Returns true if the current operation should be cancelled.
  virtual bool is_cancelled() const = 0;

  // ---- Identity ----
  virtual std::string session_id() const = 0;
  virtual std::string run_id() const = 0;
};

} // namespace merak
