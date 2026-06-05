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

class SpanParagraph : public Widget {
    std::vector<Span> spans_;
public:
    SpanParagraph(std::vector<Span> spans) : spans_(std::move(spans)) {}

    Buffer render(Rect area) override {
        Buffer buf;
        // Tokenize spans into styled atoms (words, spaces, newlines)
        struct Atom { std::string text; Style style; };
        std::vector<Atom> atoms;
        for (const auto& sp : spans_) {
            if (sp.text.empty()) continue;
            std::string word;
            for (size_t i = 0; i < sp.text.size(); ) {
                char c = sp.text[i];
                if (c == ' ' || c == '\n') {
                    if (!word.empty()) { atoms.push_back({word, sp.style}); word.clear(); }
                    atoms.push_back({c == '\n' ? "\n" : " ", sp.style});
                    ++i;
                } else {
                    // Grab the full UTF-8 sequence for this codepoint
                    auto lead = static_cast<unsigned char>(c);
                    int len = 1;
                    if ((lead & 0xE0) == 0xC0) len = 2;
                    else if ((lead & 0xF0) == 0xE0) len = 3;
                    else if ((lead & 0xF8) == 0xF0) len = 4;
                    if (i + len > sp.text.size()) len = static_cast<int>(sp.text.size() - i);
                    word.append(sp.text, i, len);
                    i += len;
                }
            }
            if (!word.empty()) atoms.push_back({word, sp.style});
        }

        uint16_t y = 0, x = 0;
        for (const auto& atom : atoms) {
            if (atom.text == "\n") {
                x = 0; ++y;
                if (y >= area.h) break;
                continue;
            }
            if (x > 0 && x + atom.text.size() > area.w) {
                x = 0; ++y;
                if (y >= area.h) break;
            }
            if (x + atom.text.size() <= area.w) {
                buf.set_span(x, y, atom.text, atom.style);
                x += atom.text.size();
            }
        }
        uint16_t h = std::min<uint16_t>(y + 1, area.h);
        buf.resize(area.w, h);
        return buf;
    }
};

} // namespace merak::tui
