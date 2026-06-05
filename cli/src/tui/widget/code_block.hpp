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

    static std::vector<std::string> split_lines_code(const std::string& text) {
        std::vector<std::string> lines;
        size_t start = 0;
        for (size_t i = 0; i < text.size(); ++i) {
            if (text[i] == '\n') {
                lines.push_back(text.substr(start, i - start));
                start = i + 1;
            }
        }
        if (start < text.size()) lines.push_back(text.substr(start));
        else if (start == text.size()) lines.push_back("");
        return lines;
    }

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
        uint16_t inner_w = area.w > 2 ? area.w - 2 : area.w;
        buf.resize(area.w, area.h);

        // Draw border from Block base
        buf = Block::render(area);

        auto lines = split_lines_code(code_);
        uint16_t y = 1;
        uint16_t max_h = area.h > 1 ? area.h - 1 : 0;

        for (size_t line_idx = 0; line_idx < lines.size() && y < max_h; ++line_idx) {
            const auto& line = lines[line_idx];
            uint32_t line_start_byte = 0;
            for (size_t prev = 0; prev < line_idx; ++prev)
                line_start_byte += static_cast<uint32_t>(lines[prev].size() + 1);
            uint32_t line_end_byte = line_start_byte + static_cast<uint32_t>(line.size());

            uint16_t x = 1;
            uint32_t pos = line_start_byte;
            for (const auto& span : cached_spans_) {
                if (span.end_byte <= line_start_byte) continue;
                if (span.start_byte >= line_end_byte) break;

                uint32_t seg_start = std::max(span.start_byte, line_start_byte);
                uint32_t seg_end = std::min(span.end_byte, line_end_byte);

                if (seg_start > pos) {
                    auto text = code_.substr(pos, seg_start - pos);
                    buf.set_span(x, y, text, Style{});
                    x += display_width(text);
                }
                if (seg_end > seg_start) {
                    auto text = code_.substr(seg_start, seg_end - seg_start);
                    auto style = theme_.style_for(span.token);
                    buf.set_span(x, y, text, style);
                    x += display_width(text);
                }
                pos = seg_end;
            }
            if (pos < line_end_byte && x < inner_w) {
                auto text = code_.substr(pos, line_end_byte - pos);
                buf.set_span(x, y, text, Style{});
            }
            ++y;
        }

        return buf;
    }
};

} // namespace merak::tui
