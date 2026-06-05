#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace merak::tui {

struct Style {
    uint8_t fg = 252;       // 256-color palette index for foreground
    uint8_t bg = 255;       // 256-color palette index for background (255 = terminal default)
    uint8_t flags = 0;      // bit flags: bold(1), dim(2), italic(4), underline(8)

    static constexpr uint8_t BOLD      = 1 << 0;
    static constexpr uint8_t DIM       = 1 << 1;
    static constexpr uint8_t ITALIC    = 1 << 2;
    static constexpr uint8_t UNDERLINE = 1 << 3;

    bool bold()      const { return flags & BOLD; }
    bool dim()       const { return flags & DIM; }
    bool italic()    const { return flags & ITALIC; }
    bool underline() const { return flags & UNDERLINE; }

    Style& bold(bool v)      { if (v) flags |= BOLD;      else flags &= ~BOLD;      return *this; }
    Style& dim(bool v)       { if (v) flags |= DIM;       else flags &= ~DIM;       return *this; }
    Style& italic(bool v)    { if (v) flags |= ITALIC;    else flags &= ~ITALIC;    return *this; }
    Style& underline(bool v) { if (v) flags |= UNDERLINE; else flags &= ~UNDERLINE; return *this; }

    bool operator==(const Style&) const = default;
};

} // namespace merak::tui
