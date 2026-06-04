#pragma once
#include "../../theme/theme.hpp"
#include <merak/message.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace merak::tui {

inline int utf8_sequence_length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 0;
}

inline std::string sanitize_terminal_text(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        auto c = static_cast<unsigned char>(input[i]);
        // Strip ANSI escape sequences
        if (c == 0x1b || c == 0x9b) {
            if (c == 0x1b && i + 1 < input.size() && input[i + 1] == ']') {
                i += 2;
                while (i < input.size()) {
                    if (input[i] == '\a') break;
                    if (input[i] == '\x1b' && i + 1 < input.size() && input[i + 1] == '\\') {
                        ++i; break;
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
        // Preserve valid UTF-8 multi-byte sequences
        if (c >= 0x80) {
            int seq_len = utf8_sequence_length(c);
            if (seq_len > 1) {
                bool valid = true;
                for (int j = 1; j < seq_len; ++j) {
                    if (i + j >= input.size() ||
                        (static_cast<unsigned char>(input[i + j]) & 0xC0) != 0x80) {
                        valid = false;
                        break;
                    }
                }
                if (valid) {
                    for (int j = 0; j < seq_len; ++j) out.push_back(input[i + j]);
                    i += seq_len - 1;
                }
            }
            continue;
        }
        // Printable ASCII
        if (c == '\n' || c == '\t' || (c >= 0x20 && c != 0x7f)) {
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

inline std::string ansi(std::string_view style, std::string_view text) {
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
    int frozen_gutter_ = 178;

    static std::string render_inline(std::string line) {
        // Headings
        if (line.starts_with("# ")) return ansi(theme::ANSI_BOLD, line.substr(2));
        if (line.starts_with("## ")) return ansi(theme::ANSI_BOLD, line.substr(3));

        // Blockquote: strip prefix, recursively inline-parse remainder
        if (line.starts_with("> ")) {
            return ansi(theme::ANSI_DIM, "│ ") + render_inline(line.substr(2));
        }

        // Inline parsing: `code`, **bold**, __italic__
        bool in_code = false;
        bool in_bold = false;
        bool in_italic = false;
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
            } else if (c == '_' && i + 1 < line.size() && line[i + 1] == '_') {
                out += in_italic ? theme::ANSI_RESET : theme::ANSI_ACCENT;
                in_italic = !in_italic;
                ++i;
            } else {
                out.push_back(c);
            }
        }
        if (in_code || in_bold || in_italic) out += theme::ANSI_RESET;

        // List items: replace leading "* " or "- " with bullet AFTER inline parsing
        if (line.starts_with("* ") || line.starts_with("- ")) {
            return ansi(theme::ANSI_ACCENT, "• ") + out.substr(2);
        }

        return out;
    }
    static bool is_separator_row(const std::string& line) {
        if (line.find('|') == std::string::npos) return false;
        for (char c : line) {
            if (c != '|' && c != '-' && c != ':' && c != ' ') return false;
        }
        return true;
    }
    static std::string highlight_code(std::string line, const std::string& language) {
        static const std::set<std::string> keywords = {
            "auto", "bool", "break", "case", "class", "const", "def", "else", "fn",
            "for", "func", "function", "if", "import", "int", "let", "package",
            "pub", "return", "std", "struct", "var", "void", "while"
        };
        (void)language;
        std::string out;
        std::string word;
        auto flush = [&] {
            if (word.empty()) return;
            out += keywords.contains(word) ? ansi(theme::ANSI_ACCENT, word) : word;
            word.clear();
        };
        for (char c : line) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') word.push_back(c);
            else { flush(); out.push_back(c); }
        }
        flush();
        return out;
    }
    static std::string table_line(std::string line) {
        std::replace(line.begin(), line.end(), '|', ' ');
        return ansi(theme::ANSI_DIM, "│ ") + line;
    }
    static std::vector<std::string> split_table_cells(const std::string& line) {
        std::vector<std::string> cells;
        std::string cell;
        size_t start = 0;
        size_t end = line.size();
        if (start < end && line[start] == '|') ++start;
        if (end > start && line[end - 1] == '|') --end;
        for (size_t i = start; i <= end; ++i) {
            if (i == end || line[i] == '|') {
                while (!cell.empty() && std::isspace(static_cast<unsigned char>(cell.front()))) cell.erase(cell.begin());
                while (!cell.empty() && std::isspace(static_cast<unsigned char>(cell.back()))) cell.pop_back();
                cells.push_back(cell);
                cell.clear();
            } else {
                cell.push_back(line[i]);
            }
        }
        return cells;
    }
    static std::string render_table_row(const std::vector<std::string>& cells,
                                        const std::vector<size_t>& widths,
                                        bool header) {
        std::string out = "│";
        for (size_t i = 0; i < widths.size(); ++i) {
            auto text = i < cells.size() ? cells[i] : "";
            out += " " + text + repeat_text(" ", widths[i] > text.size() ? widths[i] - text.size() : 0) + " │";
        }
        return ansi(header ? theme::ANSI_ACCENT : theme::ANSI_DIM, out);
    }
    int live_gutter_color() const {
        if (!live_) return frozen_gutter_;
        const auto tick = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count() / 250);
        return 178 + (tick % 36);
    }

public:
    void append(std::string_view delta) { markdown_ += sanitize_terminal_text(delta); }
    const std::string& markdown() const { return markdown_; }
    std::vector<std::string> render(size_t) const override {
        std::vector<std::string> lines;
        bool in_code = false;
        bool in_thinking = false;
        int thinking_lines = 0;
        std::string language;
        auto raw_lines = split_lines(markdown_);
        for (size_t index = 0; index < raw_lines.size(); ++index) {
            auto line = raw_lines[index];
            if (line.starts_with("```")) {
                in_code = !in_code;
                if (in_code) {
                    language = line.size() > 3 ? line.substr(3) : "";
                    lines.push_back(ansi(theme::ANSI_DIM, "┌ code " + language));
                } else {
                    lines.push_back(ansi(theme::ANSI_DIM, "└"));
                    language.clear();
                }
                continue;
            }
            if (line == "<think>" || line == "<thinking>") {
                in_thinking = true;
                thinking_lines = 0;
                continue;
            }
            if (line == "</think>" || line == "</thinking>") {
                in_thinking = false;
                lines.push_back(ansi(theme::ANSI_DIM,
                    "thinking hidden · " + std::to_string(thinking_lines) + " lines"));
                continue;
            }
            if (in_thinking) {
                ++thinking_lines;
                if (live_) lines.push_back(ansi(theme::ANSI_DIM, "thinking..."));
                continue;
            }
            if (in_code) lines.push_back("  " + highlight_code(line, language));
            else if (index + 1 < raw_lines.size()
                && line.find('|') != std::string::npos
                && is_separator_row(raw_lines[index + 1])) {
                std::vector<std::vector<std::string>> rows{split_table_cells(line)};
                index += 2;
                while (index < raw_lines.size() && raw_lines[index].find('|') != std::string::npos) {
                    rows.push_back(split_table_cells(raw_lines[index]));
                    ++index;
                }
                if (index < raw_lines.size()) --index;
                size_t columns = 0;
                for (const auto& row : rows) columns = std::max(columns, row.size());
                std::vector<size_t> widths(columns, 0);
                for (const auto& row : rows) {
                    for (size_t c = 0; c < row.size(); ++c) widths[c] = std::max(widths[c], row[c].size());
                }
                if (!rows.empty()) {
                    lines.push_back(render_table_row(rows.front(), widths, true));
                    std::string rule = "├";
                    for (auto width : widths) rule += repeat_text("─", width + 2) + "┼";
                    if (!rule.empty()) rule.back() = '┤';
                    lines.push_back(ansi(theme::ANSI_DIM, rule));
                    for (size_t r = 1; r < rows.size(); ++r) lines.push_back(render_table_row(rows[r], widths, false));
                }
            }
            else if (is_separator_row(line)) lines.push_back(ansi(theme::ANSI_DIM, "├" + repeat_text("─", std::min<size_t>(line.size(), 80))));
            else if (line.find('|') != std::string::npos) lines.push_back(table_line(line));
            else if (line.starts_with("+")) lines.push_back(ansi(theme::ANSI_SUCCESS, line));
            else if (line.starts_with("-")) lines.push_back(ansi(theme::ANSI_ERROR, line));
            else lines.push_back(render_inline(line));
        }
        auto gutter_style = "\x1b[38;5;" + std::to_string(live_gutter_color()) + "m";
        auto gutter = ansi(gutter_style.c_str(), "█ ");
        for (auto& line : lines) line = gutter + line;
        if (live_ && !lines.empty()) lines.back() += ansi(theme::ANSI_ACCENT, "▎");
        return lines;
    }
    nlohmann::json to_json() const override {
        return {{"type", "assistant"}, {"markdown", markdown_}};
    }
    bool is_live() const override { return live_; }
    void finalize() override {
        frozen_gutter_ = 178 + (static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count() / 250) % 36);
        live_ = false;
    }
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
        const std::string& color = status_ == Status::Running ? theme::ANSI_WARNING
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

    static std::string format_worldbuilding_result(const std::string& json_str) {
        try {
            auto j = nlohmann::json::parse(json_str);
            if (!j.value("ok", false)) {
                return j.value("error", "Unknown error");
            }

            std::ostringstream out;

            if (j.contains("worlds")) {
                out << "Worlds:\n";
                for (const auto& w : j["worlds"]) {
                    out << "  " << w.value("name", "") << "  ["
                        << w.value("id", "") << "]";
                    if (!w.value("description", "").empty())
                        out << "\n    " << w.value("description", "");
                    out << "\n";
                }
                return out.str();
            }

            if (j.contains("agents")) {
                out << "Agents:\n";
                for (const auto& a : j["agents"]) {
                    out << "  " << a.value("name", "") << "  ["
                        << a.value("id", "") << "]";
                    if (!a.value("display_name", "").empty())
                        out << "  (" << a.value("display_name", "") << ")";
                    out << "\n";
                }
                return out.str();
            }

            if (j.contains("agent_id") && j.contains("name")) {
                out << "Agent created: " << j.value("name", "")
                    << "  [" << j.value("agent_id", "") << "]";
                return out.str();
            }

            if (j.contains("world_id") && j.contains("name")) {
                out << "World created: " << j.value("name", "")
                    << "  [" << j.value("world_id", "") << "]";
                return out.str();
            }

            if (j.contains("scene_id")) {
                out << "Scene: [" << j.value("scene_id", "") << "]";
                if (j.contains("wrapup")) out << "\n" << j.value("wrapup", "");
                return out.str();
            }

            if (j.contains("prompt")) {
                out << "System prompt loaded";
                return out.str();
            }

            return j.dump(2);
        } catch (...) {
            return json_str;
        }
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
