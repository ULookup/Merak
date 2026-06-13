#pragma once
#include <merak/message.hpp>
#include <merak/interruption.hpp>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace merak {

enum class TurnState;

class CancellationToken {
public:
    void cancel() { cancelled_.store(true); }
    bool cancelled() const { return cancelled_.load(); }

    // 统一的中断检查入口。当前等价于 cancelled()，未来可扩展 pause 等。
    bool should_stop() const { return cancelled_.load(); }

private:
    std::atomic<bool> cancelled_ = false;
};

struct ToolExecutionContext {
    std::shared_ptr<CancellationToken> cancellation;
    std::string world_id;
    std::string scene_id;
    std::string caller_agent_id;
};

enum class LlmErrorClass : uint8_t {
  None,
  ContextWindow,   // token limit exceeded — escalate compaction
  RateLimit,       // 429 — exponential backoff
  StreamIdle,      // SSE stream went silent — retry
  StreamTransport, // SSE connection dropped — retry
  Auth,            // 401/403 — stop, no retry
  Cancelled,       // user cancelled — stop
  Unknown,         // unclassified — retry once
};

class RunControl {
public:
    virtual ~RunControl() = default;
    virtual void emit_state(TurnState from, TurnState to) = 0;
    virtual void emit_text_delta(std::string text) = 0;
    virtual void emit_tool_started(const ToolCall& call) = 0;
    virtual void emit_tool_completed(const ToolCall& call, const ToolResult& result) = 0;
    virtual bool await_approval(const ToolCall& call) = 0;
    virtual ToolResult await_creation(const ToolCall& call, const ToolResult& preliminary_result) = 0;
    virtual ToolResult await_ask_user(const ToolCall& call, const ToolResult& pending_result) = 0;
    virtual void emit_usage(int input_tokens, int output_tokens, bool exact) = 0;
    virtual void append_message(const Message& message) = 0;
    virtual void record_interruption(InterruptionRecord rec) = 0;
    virtual void record_compaction(int replaced_count) = 0;
    virtual bool cancelled() const = 0;
    virtual std::shared_ptr<CancellationToken> cancellation_token() const = 0;
    std::function<void(int turn_index, const std::string& turn_state_json,
                       int64_t input_tokens, int64_t output_tokens,
                       const std::string& pending_calls_json,
                       const std::string& compacted_summary,
                       const std::string& pipeline_snapshot_json)> save_checkpoint;
};

class NullRunControl final : public RunControl {
public:
    NullRunControl() : token_(std::make_shared<CancellationToken>()) {}
    void emit_state(TurnState, TurnState) override {}
    void emit_text_delta(std::string) override {}
    void emit_tool_started(const ToolCall&) override {}
    void emit_tool_completed(const ToolCall&, const ToolResult&) override {}
    bool await_approval(const ToolCall&) override { return true; }
    ToolResult await_creation(const ToolCall& call, const ToolResult&) override {
        ToolResult r;
        r.call_id = call.id;
        r.output = R"({"ok":true,"message":"null creation"})";
        return r;
    }
    ToolResult await_ask_user(const ToolCall& call, const ToolResult& pending) override {
        ToolResult r;
        r.call_id = call.id;
        r.output = R"({"status":"pending","answer":""})";
        return r;
    }
    void emit_usage(int, int, bool) override {}
    void append_message(const Message&) override {}
    void record_compaction(int) override {}
    void record_interruption(InterruptionRecord) override {}
    bool cancelled() const override { return token_->cancelled(); }
    std::shared_ptr<CancellationToken> cancellation_token() const override { return token_; }

private:
    std::shared_ptr<CancellationToken> token_;
};

} // namespace merak
