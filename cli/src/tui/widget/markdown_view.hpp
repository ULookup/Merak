#pragma once
#include "layout.hpp"
#include "text.hpp"
#include "paragraph.hpp"
#include "code_block.hpp"
#include "block.hpp"
#include "../../theme/theme.hpp"
#include <string>
#include <memory>
#include <vector>

namespace merak::tui {

class MarkdownView : public VStack {
public:
    explicit MarkdownView(const std::string& markdown) {
        parse(markdown);
    }

    static std::vector<Span> parse_inline_spans(const std::string& line, const theme::Theme& t) {
        std::vector<Span> spans;
        Style base;
        Style bold; bold.bold(true);
        Style accent; accent.fg = t.accent;

        bool in_code = false, in_bold = false, in_italic = false;
        std::string buf;
        size_t i = 0;
        while (i < line.size()) {
            if (!in_code && i + 1 < line.size() && line[i] == '*' && line[i + 1] == '*') {
                if (!buf.empty()) {
                    Style s; if (in_bold) s = bold; else if (in_italic) s = accent; else s = base;
                    spans.push_back({buf, s}); buf.clear();
                }
                in_bold = !in_bold;
                i += 2;
            } else if (!in_bold && !in_code && line[i] == '_' && i + 1 < line.size() && line[i + 1] == '_') {
                if (!buf.empty()) {
                    Style s; if (in_italic) s = accent; else s = base;
                    spans.push_back({buf, s}); buf.clear();
                }
                in_italic = !in_italic;
                i += 2;
            } else if (!in_bold && !in_italic && line[i] == '`') {
                if (!buf.empty()) {
                    spans.push_back({buf, base}); buf.clear();
                }
                in_code = !in_code;
                ++i;
            } else {
                auto lead = static_cast<unsigned char>(line[i]);
                int len = 1;
                if ((lead & 0xE0) == 0xC0) len = 2;
                else if ((lead & 0xF0) == 0xE0) len = 3;
                else if ((lead & 0xF8) == 0xF0) len = 4;
                if (i + len > line.size()) len = static_cast<int>(line.size() - i);
                buf.append(line, i, len);
                i += len;
            }
        }
        if (!buf.empty()) {
            Style s; if (in_code) s = accent; else if (in_bold) s = bold; else if (in_italic) s = accent; else s = base;
            spans.push_back({buf, s});
        }
        return spans;
    }

private:
    void parse(const std::string& md) {
        auto& t = theme::active_theme();
        Style bold; bold.bold(true);
        Style dim; dim.fg = t.dim; dim.dim(true);
        Style accent; accent.fg = t.accent;

        auto lines = split_lines_md(md);
        size_t i = 0;
        while (i < lines.size()) {
            auto line = lines[i];
            if (line.starts_with("```")) {
                std::string lang = line.size() > 3 ? line.substr(3) : "";
                std::string code;
                ++i;
                while (i < lines.size() && !lines[i].starts_with("```")) {
                    code += lines[i] + "\n";
                    ++i;
                }
                ++i;
                add(std::make_unique<CodeBlock>(code, lang));
            } else if (line.starts_with("# ")) {
                add(std::make_unique<Text>("  " + line.substr(2), bold));
                ++i;
            } else if (line.starts_with("## ")) {
                add(std::make_unique<Text>("  " + line.substr(3), bold));
                ++i;
            } else if (line.starts_with("> ")) {
                auto inner_spans = parse_inline_spans(line.substr(2), t);
                for (auto& sp : inner_spans) {
                    if (sp.style == Style{}) sp.style = dim;
                }
                add(std::make_unique<SpanParagraph>(std::move(inner_spans)));
                ++i;
            } else if (line.starts_with("- ") || line.starts_with("* ")) {
                add(std::make_unique<Text>("  " + line, accent));
                ++i;
            } else if (line.starts_with("+ ")) {
                Style green; green.fg = t.success;
                add(std::make_unique<Text>("  " + line, green));
                ++i;
            } else if (line.empty()) {
                ++i;
            } else {
                std::string para;
                while (i < lines.size() && !lines[i].empty()
                    && !lines[i].starts_with("```")
                    && !lines[i].starts_with("#")
                    && !lines[i].starts_with("> ")
                    && !lines[i].starts_with("- ")
                    && !lines[i].starts_with("* ")
                    && !lines[i].starts_with("+ ")) {
                    if (!para.empty()) para += " ";
                    para += lines[i];
                    ++i;
                }
                if (!para.empty()) {
                    auto spans = parse_inline_spans(para, t);
                    add(std::make_unique<SpanParagraph>(std::move(spans)));
                }
            }
        }
    }

    static std::vector<std::string> split_lines_md(const std::string& text) {
        std::vector<std::string> lines;
        std::string line;
        for (char c : text) {
            if (c == '\n') { lines.push_back(line); line.clear(); }
            else line.push_back(c);
        }
        if (!line.empty()) lines.push_back(line);
        return lines;
    }
};

} // namespace merak::tui
