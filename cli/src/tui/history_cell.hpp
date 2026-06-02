#pragma once
#include <merak/message.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace merak::tui {

inline std::string flatten_preview(std::string text, size_t max = 72) {
    std::replace(text.begin(), text.end(), '\n', ' ');
    std::replace(text.begin(), text.end(), '\r', ' ');
    if (text.size() <= max) return text;
    return text.substr(0, max - 3) + "...";
}

inline std::string summarize_tool(const ToolCall& call) {
    try {
        auto args = nlohmann::json::parse(call.arguments.empty() ? "{}" : call.arguments);
        auto value = [&](const char* key) -> std::string {
            return args.contains(key) && args[key].is_string()
                ? args[key].get<std::string>() : "";
        };
        if (call.name == "grep") {
            auto pattern = value("pattern");
            auto path = value("path");
            return flatten_preview(pattern + (path.empty() ? "" : " in " + path));
        }
        for (auto* key : {"path", "command", "query", "pattern"}) {
            auto detail = value(key);
            if (!detail.empty()) return flatten_preview(detail);
        }
    } catch (...) {
    }
    return "";
}

class HistoryCell {
public:
    virtual ~HistoryCell() = default;
    virtual std::vector<std::string> lines() const = 0;
    virtual bool persisted() const { return true; }
};

class RawCell final : public HistoryCell {
    std::vector<std::string> lines_;
public:
    explicit RawCell(std::vector<std::string> lines) : lines_(std::move(lines)) {}
    std::vector<std::string> lines() const override { return lines_; }
};

class UserCell final : public HistoryCell {
    std::string text_;
public:
    explicit UserCell(std::string text) : text_(std::move(text)) {}
    std::vector<std::string> lines() const override { return {"> " + text_}; }
};

class AssistantCell final : public HistoryCell {
    std::string text_;
    bool live_ = true;
public:
    void append(const std::string& delta) { text_ += delta; }
    void finish() { live_ = false; }
    bool live() const { return live_; }
    bool empty() const { return text_.empty(); }
    std::vector<std::string> lines() const override {
        std::vector<std::string> out;
        std::istringstream stream(text_);
        std::string line;
        while (std::getline(stream, line)) out.push_back("  " + line);
        if (out.empty()) out.push_back("  ");
        return out;
    }
};

class ToolCell final : public HistoryCell {
    ToolCall call_;
    ToolResult result_;
    std::chrono::milliseconds duration_{0};
    bool finished_ = false;
public:
    explicit ToolCell(ToolCall call) : call_(std::move(call)) {}
    const std::string& call_id() const { return call_.id; }
    const std::string& name() const { return call_.name; }
    void finish(ToolResult result, std::chrono::milliseconds duration) {
        result_ = std::move(result);
        duration_ = duration;
        finished_ = true;
    }
    std::vector<std::string> lines() const override {
        auto status = !finished_ ? "*" : (result_.is_error ? "x" : "o");
        auto verb = !finished_ ? "Running " : "Ran ";
        std::vector<std::string> out{
            std::string(status) + " " + verb + call_.name
                + (finished_ ? " (" + std::to_string(duration_.count()) + "ms)" : "")
        };
        auto detail = summarize_tool(call_);
        if (!detail.empty()) out.push_back("  | " + detail);
        if (finished_ && !result_.output.empty()) {
            out.push_back("  | " + flatten_preview(result_.output));
        }
        return out;
    }
};

class SystemCell final : public HistoryCell {
    std::string text_;
public:
    explicit SystemCell(std::string text) : text_(std::move(text)) {}
    std::vector<std::string> lines() const override { return {"! " + text_}; }
};

class TurnSummaryCell final : public HistoryCell {
    int input_tokens_;
    int output_tokens_;
    int tools_;
public:
    TurnSummaryCell(int input_tokens, int output_tokens, int tools)
        : input_tokens_(input_tokens), output_tokens_(output_tokens), tools_(tools) {}
    std::vector<std::string> lines() const override {
        return {"- " + std::to_string(input_tokens_) + " in | "
            + std::to_string(output_tokens_) + " out | "
            + std::to_string(tools_) + " tools -"};
    }
};

class ApprovalCell final : public HistoryCell {
    ToolCall call_;
public:
    explicit ApprovalCell(ToolCall call) : call_(std::move(call)) {}
    bool persisted() const override { return false; }
    std::vector<std::string> lines() const override {
        auto detail = summarize_tool(call_);
        return {"? Allow " + call_.name + (detail.empty() ? "" : ": " + detail) + " ? [y/n]"};
    }
};

} // namespace merak::tui
