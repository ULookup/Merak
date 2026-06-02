#pragma once
#include "../../theme/theme.hpp"
#include <merak/message.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace merak::tui {

inline std::string sanitize_terminal_text(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        auto c = static_cast<unsigned char>(input[i]);
        if (c == 0x1b || c == 0x9b) {
            if (c == 0x1b && i + 1 < input.size() && input[i + 1] == ']') {
                i += 2;
                while (i < input.size()) {
                    if (input[i] == '\a') break;
                    if (input[i] == '\x1b' && i + 1 < input.size() && input[i + 1] == '\\') {
                        ++i;
                        break;
                    }
                    ++i;
                }
                continue;
            }
            if (c == 0x1b && i + 1 < input.size() && input[i + 1] == '[') ++i;
            while (i + 1 < input.size()) {
                auto next = static_cast<unsigned char>(input[++i]);
                if (next >= 0x40 && next <= 0x7e) break;
            }
            continue;
        }
        if (c == '\n' || c == '\t' || (c >= 0x20 && c != 0x7f && c != 0x9b)) {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

inline std::string truncate_text(std::string text, size_t max = 96) {
    if (text.size() <= max) return text;
    return text.substr(0, max > 3 ? max - 3 : 0) + "...";
}

inline std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) lines.push_back(line);
    if (lines.empty()) lines.push_back("");
    return lines;
}

inline std::string ansi(const char* style, std::string_view text) {
    return std::string(style) + std::string(text) + theme::ANSI_RESET;
}

inline std::string repeat_text(std::string_view text, size_t count) {
    std::string out;
    out.reserve(text.size() * count);
    for (size_t i = 0; i < count; ++i) out += text;
    return out;
}

class HistoryCell {
public:
    virtual ~HistoryCell() = default;
    virtual std::vector<std::string> render(size_t width) const = 0;
    virtual nlohmann::json to_json() const = 0;
    virtual bool is_live() const { return false; }
    virtual void finalize() {}
};

class UserCell final : public HistoryCell {
    std::string text_;
public:
    explicit UserCell(std::string text) : text_(sanitize_terminal_text(text)) {}
    const std::string& text() const { return text_; }
    std::vector<std::string> render(size_t) const override {
        auto lines = split_lines(text_);
        for (size_t i = 0; i < lines.size(); ++i) {
            lines[i] = (i == 0 ? ansi(theme::ANSI_ACCENT, "› ") : "  ") + lines[i];
        }
        return lines;
    }
    nlohmann::json to_json() const override {
        return {{"type", "user"}, {"text", text_}};
    }
};

class AssistantCell final : public HistoryCell {
    std::string markdown_;
    bool live_ = true;

    static std::string render_inline(std::string line) {
        if (line.starts_with("```")) return ansi(theme::ANSI_DIM, line);
        if (line.starts_with("# ")) return ansi(theme::ANSI_BOLD, line.substr(2));
        if (line.starts_with("## ")) return ansi(theme::ANSI_BOLD, line.substr(3));
        if (line.starts_with("> ")) return ansi(theme::ANSI_DIM, "│ " + line.substr(2));
        if (line.starts_with("- ") || line.starts_with("* ")) {
            return ansi(theme::ANSI_ACCENT, "• ") + line.substr(2);
        }
        bool in_code = false;
        bool in_bold = false;
        std::string out;
        for (size_t i = 0; i < line.size(); ++i) {
            auto c = line[i];
            if (c == '`') {
                out += in_code ? theme::ANSI_RESET : theme::ANSI_ACCENT;
                in_code = !in_code;
            } else if (c == '*' && i + 1 < line.size() && line[i + 1] == '*') {
                out += in_bold ? theme::ANSI_RESET : theme::ANSI_BOLD;
                in_bold = !in_bold;
                ++i;
            } else {
                out.push_back(c);
            }
        }
        if (in_code || in_bold) out += theme::ANSI_RESET;
        return out;
    }

public:
    void append(std::string_view delta) { markdown_ += sanitize_terminal_text(delta); }
    const std::string& markdown() const { return markdown_; }
    std::vector<std::string> render(size_t) const override {
        auto lines = split_lines(markdown_);
        for (auto& line : lines) line = ansi(theme::ANSI_ACCENT, "█ ") + render_inline(line);
        if (live_ && !lines.empty()) lines.back() += ansi(theme::ANSI_ACCENT, "▎");
        return lines;
    }
    nlohmann::json to_json() const override {
        return {{"type", "assistant"}, {"markdown", markdown_}};
    }
    bool is_live() const override { return live_; }
    void finalize() override { live_ = false; }
};

class ToolCell final : public HistoryCell {
public:
    enum class Status { Running, Success, Failed };

private:
    ToolCall call_;
    Status status_ = Status::Running;
    std::chrono::steady_clock::time_point started_at_ = std::chrono::steady_clock::now();
    long duration_ms_ = 0;
    std::string description_;
    std::string output_;

    static std::string summarize(const ToolCall& call) {
        try {
            auto args = nlohmann::json::parse(call.arguments.empty() ? "{}" : call.arguments);
            auto string_value = [&](const char* key) {
                return args.contains(key) && args[key].is_string()
                    ? args[key].get<std::string>() : std::string{};
            };
            if (call.name == "grep") {
                auto pattern = string_value("pattern");
                auto path = string_value("path");
                return truncate_text(sanitize_terminal_text(pattern + (path.empty() ? "" : " in " + path)));
            }
            for (auto key : {"path", "command", "query", "pattern"}) {
                auto value = string_value(key);
                if (!value.empty()) return truncate_text(sanitize_terminal_text(value));
            }
        } catch (...) {
        }
        return "";
    }

    long elapsed_ms() const {
        if (status_ != Status::Running) return duration_ms_;
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started_at_).count();
    }

public:
    explicit ToolCell(ToolCall call)
        : call_(std::move(call)), description_(summarize(call_)) {}

    const ToolCall& call() const { return call_; }
    const std::string& output() const { return output_; }
    const std::string& description() const { return description_; }
    Status status() const { return status_; }

    void complete(const ToolResult& result) {
        duration_ms_ = elapsed_ms();
        output_ = sanitize_terminal_text(result.output);
        status_ = result.is_error ? Status::Failed : Status::Success;
    }

    std::vector<std::string> render(size_t) const override {
        const auto ms = elapsed_ms();
        const auto elapsed = ms < 1000 ? std::to_string(ms) + "ms"
                                       : std::to_string(ms / 1000.0).substr(0, 3) + "s";
        std::string verb = status_ == Status::Running ? "Running "
            : status_ == Status::Success ? "Ran " : "Failed ";
        const char* color = status_ == Status::Running ? theme::ANSI_WARNING
            : status_ == Status::Success ? theme::ANSI_SUCCESS : theme::ANSI_ERROR;
        std::vector<std::string> lines{ansi(color, "• " + verb + sanitize_terminal_text(call_.name))
            + ansi(theme::ANSI_DIM, " (" + elapsed + ")")};
        if (!description_.empty()) lines.push_back(ansi(theme::ANSI_DIM, "  │ ") + description_);
        if (status_ != Status::Running && !output_.empty()) {
            auto preview = split_lines(output_);
            const auto count = std::min<size_t>(preview.size(), 5);
            for (size_t i = 0; i < count; ++i) {
                lines.push_back(ansi(theme::ANSI_DIM, i == 0 ? "  └ " : "    ")
                    + truncate_text(preview[i], 120));
            }
            if (preview.size() > count) {
                lines.push_back(ansi(theme::ANSI_DIM,
                    "    … +" + std::to_string(preview.size() - count) + " lines"));
            }
        }
        return lines;
    }

    nlohmann::json to_json() const override {
        return {
            {"type", "tool"},
            {"id", call_.id},
            {"name", call_.name},
            {"arguments", call_.arguments},
            {"description", description_},
            {"status", status_ == Status::Success ? "success"
                : status_ == Status::Failed ? "failed" : "running"},
            {"duration_ms", elapsed_ms()},
            {"output", output_},
        };
    }
    bool is_live() const override { return status_ == Status::Running; }
    void finalize() override {
        if (status_ == Status::Running) {
            duration_ms_ = elapsed_ms();
            status_ = Status::Failed;
        }
    }
};

class SystemCell final : public HistoryCell {
    std::string text_;
    bool error_;
public:
    SystemCell(std::string text, bool error = false)
        : text_(sanitize_terminal_text(text)), error_(error) {}
    std::vector<std::string> render(size_t) const override {
        auto lines = split_lines(text_);
        for (size_t i = 0; i < lines.size(); ++i) {
            lines[i] = ansi(error_ ? theme::ANSI_ERROR : theme::ANSI_WARNING,
                std::string(i == 0 ? (error_ ? "✗ " : "ℹ ") : "  ") + lines[i]);
        }
        return lines;
    }
    nlohmann::json to_json() const override {
        return {{"type", "system"}, {"text", text_}, {"error", error_}};
    }
};

class TurnSummaryCell final : public HistoryCell {
    long elapsed_ms_;
    int input_tokens_;
    int output_tokens_;
    int tools_;
    int cumulative_tokens_;
    bool has_usage_;
public:
    TurnSummaryCell(long elapsed_ms, int input, int output, int tools,
                    int cumulative, bool has_usage)
        : elapsed_ms_(elapsed_ms), input_tokens_(input), output_tokens_(output),
          tools_(tools), cumulative_tokens_(cumulative), has_usage_(has_usage) {}
    std::vector<std::string> render(size_t) const override {
        auto usage = has_usage_
            ? "⚡ " + std::to_string(input_tokens_ + output_tokens_) + " ↑"
                + std::to_string(input_tokens_) + " ↓" + std::to_string(output_tokens_)
            : "⚡ n/a";
        return {ansi(theme::ANSI_DIM, "  ─ ⏱ " + std::to_string(elapsed_ms_) + "ms │ "
            + usage + " │ 🛠 " + std::to_string(tools_) + " │ Σ "
            + std::to_string(cumulative_tokens_) + " ─")};
    }
    nlohmann::json to_json() const override {
        return {{"type", "turn_summary"}, {"elapsed_ms", elapsed_ms_},
            {"tokens_in", input_tokens_}, {"tokens_out", output_tokens_},
            {"tools", tools_}, {"cumulative_tokens", cumulative_tokens_},
            {"has_usage", has_usage_}};
    }
};

class ApprovalCell final {
    std::string prompt_;
public:
    explicit ApprovalCell(std::string prompt) : prompt_(std::move(prompt)) {}
    std::vector<std::string> render() const {
        return {ansi(theme::ANSI_ACCENT, "⏸ " + prompt_ + "  [y] allow  [n] deny")};
    }
};

} // namespace merak::tui
