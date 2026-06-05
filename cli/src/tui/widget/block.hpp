#pragma once
#include "../widget.hpp"
#include <memory>

namespace merak::tui {

class Block : public Widget {
    std::unique_ptr<Widget> inner_;
    std::string title_;
    Style border_style_;
public:
    Block(std::unique_ptr<Widget> inner, std::string title = "", Style border = {})
        : inner_(std::move(inner)), title_(std::move(title)), border_style_(border) {}

    Buffer render(Rect area) override {
        Rect inner_area = {1, 1,
            area.w > 2 ? static_cast<uint16_t>(area.w - 2) : 0,
            area.h > 2 ? static_cast<uint16_t>(area.h - 2) : 0};
        Buffer inner_buf = inner_->render(inner_area);
        Buffer buf;
        buf.resize(area.w, area.h);
        if (area.w >= 2) {
            buf.at(0, 0).ch = U'┌';
            for (uint16_t x = 1; x < area.w - 1; ++x) buf.at(x, 0).ch = U'─';
            if (area.w > 1) buf.at(area.w - 1, 0).ch = U'┐';
            if (!title_.empty() && area.w > 2) {
                buf.set_span(2, 0, " " + title_ + " ", border_style_);
            }
        }
        for (uint16_t y = 0; y < inner_buf.h && y + 1 < area.h - 1; ++y) {
            buf.at(0, y + 1).ch = U'│';
            if (area.w > 1) buf.at(area.w - 1, y + 1).ch = U'│';
            for (uint16_t x = 0; x < inner_buf.w && x + 1 < area.w - 1; ++x) {
                buf.at(x + 1, y + 1) = inner_buf.at(x, y);
            }
        }
        if (area.h >= 2) {
            buf.at(0, area.h - 1).ch = U'└';
            for (uint16_t x = 1; x < area.w - 1; ++x) buf.at(x, area.h - 1).ch = U'─';
            if (area.w > 1) buf.at(area.w - 1, area.h - 1).ch = U'┘';
        }
        return buf;
    }
};

} // namespace merak::tui
