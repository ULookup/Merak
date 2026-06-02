#pragma once
#include <merak/message.hpp>
#include <atomic>
#include <memory>
#include <string>

namespace merak {

enum class TurnState;

class CancellationToken {
public:
    void cancel() { cancelled_.store(true); }
    bool cancelled() const { return cancelled_.load(); }

private:
    std::atomic<bool> cancelled_ = false;
};

struct ToolExecutionContext {
    std::shared_ptr<CancellationToken> cancellation;
};

class RunControl {
public:
    virtual ~RunControl() = default;
    virtual void emit_state(TurnState from, TurnState to) = 0;
    virtual void emit_text_delta(std::string text) = 0;
    virtual void emit_tool_started(const ToolCall& call) = 0;
    virtual void emit_tool_completed(const ToolCall& call, const ToolResult& result) = 0;
    virtual bool await_approval(const ToolCall& call) = 0;
    virtual void emit_usage(int input_tokens, int output_tokens, bool exact) = 0;
    virtual void append_message(const Message& message) = 0;
    virtual void record_compaction(int replaced_count) = 0;
    virtual bool cancelled() const = 0;
    virtual std::shared_ptr<CancellationToken> cancellation_token() const = 0;
};

class NullRunControl final : public RunControl {
public:
    NullRunControl() : token_(std::make_shared<CancellationToken>()) {}
    void emit_state(TurnState, TurnState) override {}
    void emit_text_delta(std::string) override {}
    void emit_tool_started(const ToolCall&) override {}
    void emit_tool_completed(const ToolCall&, const ToolResult&) override {}
    bool await_approval(const ToolCall&) override { return true; }
    void emit_usage(int, int, bool) override {}
    void append_message(const Message&) override {}
    void record_compaction(int) override {}
    bool cancelled() const override { return token_->cancelled(); }
    std::shared_ptr<CancellationToken> cancellation_token() const override { return token_; }

private:
    std::shared_ptr<CancellationToken> token_;
};

} // namespace merak
