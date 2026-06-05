#pragma once
#include "layout.hpp"
#include "text.hpp"
#include "../buffer.hpp"
#include "../../theme/theme.hpp"
#include <vector>
#include <string>

namespace merak::tui {

class DiffView : public VStack {
public:
    explicit DiffView(const std::string& diff_text) {
        parse_diff(diff_text);
    }

private:
    void parse_diff(const std::string& text) {
        auto lines = split_lines_diff(text);
        Style add_style; add_style.fg = theme::active_theme().success;
        Style del_style; del_style.fg = theme::active_theme().error;
        Style hdr_style; hdr_style.fg = 51;
        Style dim_style; dim_style.fg = theme::active_theme().dim; dim_style.dim(true);

        for (const auto& line : lines) {
            if (line.starts_with("+")) {
                add(std::make_unique<Text>("  " + line, add_style));
            } else if (line.starts_with("-")) {
                add(std::make_unique<Text>("  " + line, del_style));
            } else if (line.starts_with("@@")) {
                add(std::make_unique<Text>(line, hdr_style));
            } else {
                add(std::make_unique<Text>(line, dim_style));
            }
        }
    }

    static std::vector<std::string> split_lines_diff(const std::string& text) {
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
