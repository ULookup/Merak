#pragma once
#include "../widget.hpp"
#include <memory>
#include <vector>

namespace merak::tui {

class VStack : public Widget {
    std::vector<std::unique_ptr<Widget>> children_;
    std::vector<uint16_t> flex_;

public:
    VStack& add(std::unique_ptr<Widget> w, uint16_t flex = 0) {
        flex_.push_back(flex);
        children_.push_back(std::move(w));
        return *this;
    }

    Buffer render(Rect area) override {
        Buffer buf;
        buf.resize(area.w, area.h);

        std::vector<uint16_t> heights(children_.size());
        uint16_t total_fixed = 0;
        uint16_t total_flex = 0;
        for (size_t i = 0; i < children_.size(); ++i) {
            auto sz = children_[i]->measure(area.w);
            if (flex_[i] > 0) {
                total_flex += flex_[i];
                heights[i] = sz.min;
            } else {
                heights[i] = sz.preferred > 0 ? sz.preferred : 1;
                total_fixed += heights[i];
            }
        }

        if (total_flex > 0 && area.h > total_fixed) {
            uint16_t remaining = area.h - total_fixed;
            for (size_t i = 0; i < children_.size(); ++i) {
                if (flex_[i] > 0) {
                    heights[i] = remaining * flex_[i] / total_flex;
                }
            }
        }

        uint16_t y = 0;
        for (size_t i = 0; i < children_.size() && y < area.h; ++i) {
            uint16_t child_h = std::min(heights[i], static_cast<uint16_t>(area.h - y));
            Rect child_area = {0, 0, area.w, child_h};
            auto child_buf = children_[i]->render(child_area);
            for (uint16_t cy = 0; cy < child_buf.h && cy < child_h; ++cy) {
                for (uint16_t cx = 0; cx < child_buf.w && cx < area.w; ++cx) {
                    buf.at(cx, y + cy) = child_buf.at(cx, cy);
                }
            }
            y += child_h;
        }
        return buf;
    }
};

class HStack : public Widget {
    std::vector<std::unique_ptr<Widget>> children_;
public:
    HStack& add(std::unique_ptr<Widget> w) {
        children_.push_back(std::move(w));
        return *this;
    }

    Buffer render(Rect area) override {
        Buffer buf;
        buf.resize(area.w, area.h);
        uint16_t x = 0;
        for (auto& child : children_) {
            auto sz = child->measure(area.w - x);
            uint16_t child_w = sz.preferred > 0 ? std::min(sz.preferred,
                static_cast<uint16_t>(area.w - x)) : (area.w - x) / children_.size();
            Rect child_area = {0, 0, child_w, area.h};
            auto child_buf = child->render(child_area);
            for (uint16_t cy = 0; cy < child_buf.h && cy < area.h; ++cy) {
                for (uint16_t cx = 0; cx < child_buf.w && x + cx < area.w; ++cx) {
                    buf.at(x + cx, cy) = child_buf.at(cx, cy);
                }
            }
            x += child_w;
            if (x >= area.w) break;
        }
        return buf;
    }
};

} // namespace merak::tui
