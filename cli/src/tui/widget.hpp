#pragma once
#include "buffer.hpp"
#include <cstdint>
#include <optional>

namespace merak::tui {

struct Rect {
    uint16_t x = 0, y = 0, w = 0, h = 0;
};

struct Size {
    uint16_t min = 0;
    uint16_t preferred = 0;
    uint16_t max = UINT16_MAX;
};

class Widget {
public:
    virtual ~Widget() = default;
    virtual Buffer render(Rect area) = 0;
    virtual Size measure(uint16_t max_w) const {
        return {0, 0, UINT16_MAX};
    }
    virtual bool is_container() const { return false; }
    virtual std::optional<Rect> child_area(size_t /*idx*/, Rect self) const {
        return std::nullopt;
    }
};

} // namespace merak::tui
