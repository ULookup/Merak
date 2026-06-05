#pragma once
#include "../../theme/theme.hpp"
#include "../buffer.hpp"
#include "../widget/markdown_view.hpp"
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

inline void write_spans(Buffer& buf, const std::vector<std::vector<Span>>& lines) {
    for (uint16_t y = 0; y < lines.size() && y < buf.h; ++y) {
        uint16_t x = 0;
        for (const auto& span : lines[y]) {
            buf.set_span(x, y, span.text, span.style);
            size_t pos = 0;
            while (pos < span.text.size()) {
                auto cp = utf8_decode(span.text, pos);
                uint8_t cw = char_width(cp);
                if (cw == 0 && x > 0) continue;
                x += cw > 0 ? cw : uint16_t(1);
            }
        }
    }
}

class HistoryCell {
public:
    virtual ~HistoryCell() = default;
    virtual void render(Buffer& buf, uint16_t width) const = 0;
    virtual nlohmann::json to_json() const = 0;
    virtual bool is_live() const { return false; }
    virtual void finalize() {}
};

class UserCell final : public HistoryCell {
    std::string text_;
public:
    explicit UserCell(std::string text) : text_(sanitize_terminal_text(text)) {}
    const std::string& text() const { return text_; }
    void render(Buffer& buf, uint16_t width) const override {
        (void)width;
        auto lines = split_lines(text_);
        auto& t = theme::active_theme();
        Style accent; accent.fg = t.accent; accent.bold(true);
        Style dim_fg; dim_fg.fg = t.dim; dim_fg.dim(true);
        std::vector<std::vector<Span>> spans(lines.size());
        for (size_t i = 0; i < lines.size(); ++i) {
            spans[i].push_back({i == 0 ? "> " : "  ", accent});
            spans[i].push_back({lines[i], Style{}});
        }
        if (buf.h < lines.size()) buf.resize(buf.w, lines.size());
        write_spans(buf, spans);
    }
    nlohmann::json to_json() const override {
        return {{"type", "user"}, {"text", text_}};
    }
};

class AssistantCell final : public HistoryCell {
    std::string markdown_;
    bool live_ = true;
    int frozen_gutter_ = 178;

    static bool is_separator_row(const std::string& line) {
        if (line.find('|') == std::string::npos) return false;
        for (char c : line) {
            if (c != '|' && c != '-' && c != ':' && c != ' ') return false;
        }
        return true;
    }
    static std::vector<Span> highlight_code_spans(std::string line, const std::string& language, const theme::Theme& t) {
        static const std::set<std::string> keywords = {
            "auto", "bool", "break", "case", "class", "const", "def", "else", "fn",
            "for", "func", "function", "if", "import", "int", "let", "package",
            "pub", "return", "std", "struct", "var", "void", "while"
        };
        (void)language;
        std::vector<Span> spans;
        Style accent; accent.fg = t.accent;
        Style base;
        std::string word;
        auto flush = [&] {
            if (word.empty()) return;
            spans.push_back({word, keywords.contains(word) ? accent : base});
            word.clear();
        };
        for (char c : line) {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') word.push_back(c);
            else { flush(); spans.push_back({std::string(1, c), base}); }
        }
        flush();
        return spans;
    }
    static std::vector<Span> table_line_spans(std::string line, const theme::Theme& t) {
        std::replace(line.begin(), line.end(), '|', ' ');
        Style dim_fg; dim_fg.fg = t.dim; dim_fg.dim(true);
        return {Span{"│ " + line, dim_fg}};
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
    static std::vector<Span> render_table_row_spans(const std::vector<std::string>& cells,
                                                      const std::vector<size_t>& widths,
                                                      bool header, const theme::Theme& t) {
        Style style;
        if (header) { style.fg = t.accent; } else { style.fg = t.dim; style.dim(true); }
        std::string text = "│";
        for (size_t i = 0; i < widths.size(); ++i) {
            auto cell_text = i < cells.size() ? cells[i] : "";
            text += " " + cell_text + repeat_text(" ", widths[i] > cell_text.size() ? widths[i] - cell_text.size() : 0) + " │";
        }
        return {Span{text, style}};
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
    void render(Buffer& buf, uint16_t width) const override {
        (void)width;
        auto& t = theme::active_theme();
        Style accent; accent.fg = t.accent;
        Style dim_fg; dim_fg.fg = t.dim; dim_fg.dim(true);
        Style success_fg; success_fg.fg = t.success;
        Style error_fg; error_fg.fg = t.error;
        Style base;

        std::vector<std::vector<Span>> lines;
        bool in_code = false, in_thinking = false;
        int thinking_lines = 0;
        std::string language;
        auto raw_lines = split_lines(markdown_);

        for (size_t index = 0; index < raw_lines.size(); ++index) {
            auto line = raw_lines[index];
            if (line.starts_with("```")) {
                in_code = !in_code;
                if (in_code) {
                    language = line.size() > 3 ? line.substr(3) : "";
                    lines.push_back({Span{"┌ code " + language, dim_fg}});
                } else {
                    lines.push_back({Span{"└", dim_fg}});
                    language.clear();
                }
                continue;
            }
            if (line == "<think>" || line == "<thinking>") {
                in_thinking = true; thinking_lines = 0; continue;
            }
            if (line == "</think>" || line == "</thinking>") {
                in_thinking = false;
                lines.push_back({Span{"thinking hidden · " + std::to_string(thinking_lines) + " lines", dim_fg}});
                continue;
            }
            if (in_thinking) { ++thinking_lines; if (live_) lines.push_back({Span{"thinking...", dim_fg}}); continue; }

            if (in_code) {
                std::vector<Span> code_line;
                code_line.push_back({"  ", base});
                auto hl = highlight_code_spans(line, language, t);
                code_line.insert(code_line.end(), hl.begin(), hl.end());
                lines.push_back(code_line);
            } else if (index+1 < raw_lines.size() && line.find('|') != std::string::npos && is_separator_row(raw_lines[index+1])) {
                // Table handling — build spans per row
                std::vector<std::vector<std::string>> rows{split_table_cells(line)};
                index += 2;
                while (index < raw_lines.size() && raw_lines[index].find('|') != std::string::npos) {
                    rows.push_back(split_table_cells(raw_lines[index])); ++index;
                }
                if (index < raw_lines.size()) --index;
                size_t columns = 0;
                for (const auto& row : rows) columns = std::max(columns, row.size());
                std::vector<size_t> widths(columns, 0);
                for (const auto& row : rows) for (size_t c=0; c<row.size(); ++c) widths[c]=std::max(widths[c], row[c].size());
                if (!rows.empty()) {
                    lines.push_back(render_table_row_spans(rows.front(), widths, true, t));
                    std::string rule = "├";
                    for (auto w : widths) rule += repeat_text("─", w+2) + "┼";
                    if (!rule.empty()) rule.back() = '┤';
                    lines.push_back({Span{rule, dim_fg}});
                    for (size_t r=1; r<rows.size(); ++r) lines.push_back(render_table_row_spans(rows[r], widths, false, t));
                }
            } else if (is_separator_row(line)) {
                lines.push_back({Span{"├" + repeat_text("─", std::min<size_t>(line.size(), 80)), dim_fg}});
            } else if (line.find('|') != std::string::npos) {
                lines.push_back(table_line_spans(line, t));
            } else if (line.starts_with("+")) {
                lines.push_back({Span{line, success_fg}});
            } else if (line.starts_with("-")) {
                lines.push_back({Span{line, error_fg}});
            } else if (line.starts_with("# ")) {
                Style bold; bold.bold(true);
                lines.push_back({Span{line.substr(2), bold}});
            } else if (line.starts_with("## ")) {
                Style bold; bold.bold(true);
                lines.push_back({Span{line.substr(3), bold}});
            } else if (line.starts_with("> ")) {
                std::vector<Span> bq;
                bq.push_back({"│ ", dim_fg});
                auto inner = MarkdownView::parse_inline_spans(line.substr(2), t);
                bq.insert(bq.end(), inner.begin(), inner.end());
                lines.push_back(bq);
            } else if (line.starts_with("* ") || line.starts_with("- ")) {
                auto list_spans = MarkdownView::parse_inline_spans(line, t);
                if (!list_spans.empty() && list_spans[0].text.size() >= 2) {
                    list_spans[0].text = "• " + list_spans[0].text.substr(2);
                }
                lines.push_back(list_spans);
            } else {
                lines.push_back(MarkdownView::parse_inline_spans(line, t));
            }
        }

        // Add gutter and cursor
        Style gutter_style; gutter_style.fg = live_gutter_color();
        Style cursor_style; cursor_style.fg = t.accent;
        for (auto& line_spans : lines) {
            line_spans.insert(line_spans.begin(), {"█ ", gutter_style});
        }
        if (live_ && !lines.empty()) {
            lines.back().push_back({"▎", cursor_style});
        }

        if (buf.h < lines.size()) buf.resize(buf.w, lines.size());
        write_spans(buf, lines);
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

    void render(Buffer& buf, uint16_t width) const override {
        (void)width;
        auto& t = theme::active_theme();
        const auto ms = elapsed_ms();
        const auto elapsed = ms < 1000 ? std::to_string(ms) + "ms"
                                       : std::to_string(ms / 1000.0).substr(0, 3) + "s";
        std::string verb = status_ == Status::Running ? "Running "
            : status_ == Status::Success ? "Ran " : "Failed ";

        Style status_style;
        if (status_ == Status::Running) status_style.fg = t.warn;
        else if (status_ == Status::Success) status_style.fg = t.success;
        else status_style.fg = t.error;

        Style dim_fg; dim_fg.fg = t.dim; dim_fg.dim(true);
        Style base;

        std::vector<std::vector<Span>> lines;
        lines.push_back({Span{"• " + verb + sanitize_terminal_text(call_.name), status_style},
                         Span{" (" + elapsed + ")", dim_fg}});
        if (!description_.empty()) lines.push_back({Span{"  │ " + description_, dim_fg}});
        if (status_ != Status::Running && !output_.empty()) {
            auto preview = split_lines(output_);
            const auto count = std::min<size_t>(preview.size(), 5);
            for (size_t i = 0; i < count; ++i) {
                lines.push_back({Span{(i == 0 ? "  └ " : "    ") + truncate_text(preview[i], 120), dim_fg}});
            }
            if (preview.size() > count) {
                lines.push_back({Span{"    … +" + std::to_string(preview.size() - count) + " lines", dim_fg}});
            }
        }
        if (buf.h < lines.size()) buf.resize(buf.w, lines.size());
        write_spans(buf, lines);
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
    void render(Buffer& buf, uint16_t width) const override {
        (void)width;
        auto& t = theme::active_theme();
        Style prefix_style; prefix_style.fg = error_ ? t.error : t.warn;
        Style base;
        auto raw_lines = split_lines(text_);
        std::vector<std::vector<Span>> lines(raw_lines.size());
        for (size_t i = 0; i < raw_lines.size(); ++i) {
            lines[i].push_back({i == 0 ? (error_ ? "✗ " : "ℹ ") : "  ", prefix_style});
            lines[i].push_back({raw_lines[i], base});
        }
        if (buf.h < lines.size()) buf.resize(buf.w, lines.size());
        write_spans(buf, lines);
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
    void render(Buffer& buf, uint16_t width) const override {
        (void)width;
        auto& t = theme::active_theme();
        Style dim_fg; dim_fg.fg = t.dim; dim_fg.dim(true);
        auto usage = has_usage_
            ? "⚡ " + std::to_string(input_tokens_ + output_tokens_) + " ↑"
                + std::to_string(input_tokens_) + " ↓" + std::to_string(output_tokens_)
            : "⚡ n/a";
        std::string text = "  ─ ⏱ " + std::to_string(elapsed_ms_) + "ms │ "
            + usage + " │ 🛠 " + std::to_string(tools_) + " │ Σ "
            + std::to_string(cumulative_tokens_) + " ─";
        if (buf.h < 1) buf.resize(buf.w, 1);
        write_spans(buf, {{Span{text, dim_fg}}});
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
    void render(Buffer& buf) const {
        auto& t = theme::active_theme();
        Style accent; accent.fg = t.accent;
        if (buf.h < 1) buf.resize(buf.w, 1);
        write_spans(buf, {{Span{"⏸ " + prompt_ + "  [y] allow  [n] deny", accent}}});
    }
};

} // namespace merak::tui
