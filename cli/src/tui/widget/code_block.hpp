#pragma once
#include "block.hpp"
#include "../syntax/highlighter.hpp"
#include "../syntax/theme_map.hpp"
#include "../buffer.hpp"

namespace merak::tui {

class CodeBlock : public Block {
    std::string code_;
    std::string language_;
    syntax::Highlighter highlighter_;
    syntax::ThemeMap theme_ = syntax::ThemeMap::dark();

    mutable std::string cached_code_;
    mutable std::string cached_lang_;
    mutable std::vector<syntax::HighlightSpan> cached_spans_;

public:
    CodeBlock(std::string code, std::string language, std::unique_ptr<Widget> inner = nullptr)
        : Block(std::move(inner), language), code_(std::move(code)), language_(std::move(language)) {}

    Buffer render(Rect area) override {
        if (code_ != cached_code_ || language_ != cached_lang_) {
            auto* lang = syntax::language_for(language_);
            cached_spans_ = highlighter_.highlight(code_, lang);
            cached_code_ = code_;
            cached_lang_ = language_;
        }

        Buffer buf;
        buf.resize(area.w, area.h);

        size_t pos = 0;
        uint16_t y = 1;
        uint16_t x = 1;
        Style current_style;

        for (const auto& span : cached_spans_) {
            while (pos < span.start_byte && y < area.h - 1) {
                char c = code_[pos];
                if (c == '\n') {
                    ++y; x = 1;
                } else if (c != '\r') {
                    if (x < area.w - 1) {
                        buf.at(x, y).ch = static_cast<char32_t>(static_cast<unsigned char>(c));
                        buf.at(x, y).style = current_style;
                        ++x;
                    }
                }
                ++pos;
            }

            auto span_style = theme_.style_for(span.token);
            for (uint32_t i = span.start_byte; i < span.end_byte && y < area.h - 1; ++i) {
                char c = code_[i];
                if (c == '\n') {
                    ++y; x = 1;
                } else if (c != '\r') {
                    if (x < area.w - 1) {
                        buf.at(x, y).ch = static_cast<char32_t>(static_cast<unsigned char>(c));
                        buf.at(x, y).style = span_style;
                        ++x;
                    }
                }
            }
            pos = span.end_byte;
        }

        return buf;
    }
};

} // namespace merak::tui
