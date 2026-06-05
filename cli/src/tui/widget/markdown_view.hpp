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

private:
    void parse(const std::string& md) {
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
                Style dim; dim.fg = theme::active_theme().dim; dim.dim(true);
                add(std::make_unique<CodeBlock>(code, lang));
            } else if (line.starts_with("# ")) {
                Style bold; bold.bold(true);
                add(std::make_unique<Text>("  " + line.substr(2), bold));
                ++i;
            } else if (line.starts_with("## ")) {
                Style bold; bold.bold(true);
                add(std::make_unique<Text>("  " + line.substr(3), bold));
                ++i;
            } else if (line.starts_with("> ")) {
                Style dim; dim.fg = theme::active_theme().dim; dim.dim(true);
                add(std::make_unique<Text>(line, dim));
                ++i;
            } else if (line.starts_with("- ") || line.starts_with("* ")) {
                Style accent; accent.fg = theme::active_theme().accent;
                add(std::make_unique<Text>("  " + line, accent));
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
                    && !lines[i].starts_with("* ")) {
                    if (!para.empty()) para += " ";
                    para += lines[i];
                    ++i;
                }
                if (!para.empty()) add(std::make_unique<Paragraph>(para));
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
