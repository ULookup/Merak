#pragma once
#include "../widget.hpp"

namespace merak::tui {

class Text : public Widget {
    std::string text_;
    Style style_;
public:
    Text(std::string text, Style s = {}) : text_(std::move(text)), style_(s) {}
    Buffer render(Rect area) override {
        Buffer buf;
        buf.resize(area.w, 1);
        buf.set_span(0, 0, text_, style_);
        return buf;
    }
    Size measure(uint16_t /*max_w*/) const override {
        return {0, 1, UINT16_MAX};
    }
};

} // namespace merak::tui
