#pragma once
#include "../widget.hpp"
#include <vector>

namespace merak::tui {

class Paragraph : public Widget {
    std::string text_;
    Style style_;
public:
    Paragraph(std::string text, Style s = {}) : text_(std::move(text)), style_(s) {}

    Buffer render(Rect area) override {
        Buffer buf;
        std::vector<std::string> words;
        std::string word;
        for (char c : text_) {
            if (c == ' ' || c == '\n') {
                if (!word.empty()) { words.push_back(word); word.clear(); }
                if (c == '\n') words.push_back("\n");
                else words.push_back(" ");
            } else {
                word.push_back(c);
            }
        }
        if (!word.empty()) words.push_back(word);

        uint16_t y = 0;
        std::string line;
        for (const auto& w : words) {
            if (w == "\n") {
                buf.set_span(0, y, line, style_);
                line.clear();
                ++y;
                if (y >= area.h) break;
                continue;
            }
            if (line.size() + w.size() > area.w) {
                buf.set_span(0, y, line, style_);
                line.clear();
                ++y;
                if (y >= area.h) break;
            }
            line += w;
        }
        if (!line.empty() && y < area.h) buf.set_span(0, y, line, style_);
        uint16_t h = std::min<uint16_t>(y + 1, area.h);
        buf.resize(area.w, h);
        return buf;
    }
};

} // namespace merak::tui
