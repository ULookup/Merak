#pragma once
#include "highlighter.hpp"
#include "../buffer.hpp"
#include "../../theme/theme.hpp"
#include <nlohmann/json.hpp>
#include <unordered_map>

namespace merak::tui::syntax {

class ThemeMap {
    std::unordered_map<HighlightToken, Style> map_;

public:
    static ThemeMap dark() {
        ThemeMap tm;
        auto fg = [](uint8_t c) -> Style { Style s; s.fg = c; return s; };
        auto bold = [](uint8_t c) -> Style { Style s; s.fg = c; s.bold(true); return s; };
        auto dim = [](uint8_t c) -> Style { Style s; s.fg = c; s.dim(true); return s; };

        tm.map_ = {
            {HighlightToken::KEYWORD,     bold(141)},
            {HighlightToken::STRING,      fg(78)},
            {HighlightToken::NUMBER,      fg(221)},
            {HighlightToken::COMMENT,     dim(102)},
            {HighlightToken::TYPE,        fg(51)},
            {HighlightToken::FUNCTION,    fg(75)},
            {HighlightToken::METHOD,      fg(75)},
            {HighlightToken::PROPERTY,    fg(252)},
            {HighlightToken::VARIABLE,    fg(252)},
            {HighlightToken::PARAMETER,   fg(216)},
            {HighlightToken::OPERATOR,    fg(103)},
            {HighlightToken::PUNCTUATION, fg(252)},
            {HighlightToken::CONSTANT,    fg(221)},
            {HighlightToken::NAMESPACE,   fg(51)},
            {HighlightToken::REGEX,       fg(203)},
            {HighlightToken::ESCAPE,      fg(221)},
            {HighlightToken::LABEL,       fg(252)},
            {HighlightToken::ATTRIBUTE,   fg(252)},
            {HighlightToken::EMBEDDED,    fg(178)},
            {HighlightToken::TAG,         fg(141)},
            {HighlightToken::ERROR,       bold(203)},
            {HighlightToken::DIFF_ADD,    fg(78)},
            {HighlightToken::DIFF_DEL,    fg(203)},
            {HighlightToken::TEXT,        fg(252)},
        };
        return tm;
    }

    static ThemeMap light() {
        ThemeMap tm = dark();
        for (auto& [token, style] : tm.map_) {
            if (style.fg > 200) style.fg = 16;
            else if (style.fg < 20) style.fg = 250;
        }
        return tm;
    }

    Style style_for(HighlightToken token) const {
        auto it = map_.find(token);
        return it != map_.end() ? it->second : Style{};
    }
};

} // namespace merak::tui::syntax
