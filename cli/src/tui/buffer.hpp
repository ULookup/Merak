#pragma once
#include <algorithm>
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

// Unicode display width for a codepoint
inline uint8_t char_width(char32_t cp) {
    // Zero-width characters
    if (cp == 0x200B || cp == 0xFEFF || cp == 0x00AD) return 0;
    // Combining marks (Unicode ranges)
    if ((cp >= 0x0300 && cp <= 0x036F) ||
        (cp >= 0x1AB0 && cp <= 0x1AFF) ||
        (cp >= 0x1DC0 && cp <= 0x1DFF) ||
        (cp >= 0x20D0 && cp <= 0x20FF) ||
        (cp >= 0xFE20 && cp <= 0xFE2F)) return 0;
    // CJK and wide characters
    if ((cp >= 0x1100 && cp <= 0x115F) ||   // Hangul Jamo
        (cp >= 0x2329 && cp <= 0x232A) ||   // Tech angle brackets
        (cp >= 0x2E80 && cp <= 0xA4CF) ||   // CJK Radicals through Yi
        (cp >= 0xA960 && cp <= 0xA97C) ||   // Hangul Jamo Extended
        (cp >= 0xAC00 && cp <= 0xD7A3) ||   // Hangul Syllables
        (cp >= 0xF900 && cp <= 0xFAFF) ||   // CJK Compatibility
        (cp >= 0xFE10 && cp <= 0xFE19) ||   // Vertical forms
        (cp >= 0xFE30 && cp <= 0xFE6F) ||   // CJK Compatibility Forms
        (cp >= 0xFF01 && cp <= 0xFF60) ||   // Fullwidth Forms
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||   // Fullwidth Signs
        (cp >= 0x1F300 && cp <= 0x1F64F) || // Emoticons
        (cp >= 0x1F680 && cp <= 0x1F6FF) || // Transport
        (cp >= 0x1F900 && cp <= 0x1F9FF) || // Supplemental Symbols
        (cp >= 0x20000 && cp <= 0x2FFFD) ||  // CJK Extension B+
        (cp >= 0x30000 && cp <= 0x3FFFD)) return 2;
    return cp < 0x20 || (cp >= 0x7F && cp <= 0x9F) ? 0 : 1;
}

// Decode next UTF-8 codepoint from string, return it and advance pos
inline char32_t utf8_decode(std::string_view text, size_t& pos) {
    if (pos >= text.size()) return 0;
    auto lead = static_cast<unsigned char>(text[pos]);
    char32_t cp;
    int len;
    if (lead < 0x80) { cp = lead; len = 1; }
    else if ((lead & 0xE0) == 0xC0) { cp = lead & 0x1F; len = 2; }
    else if ((lead & 0xF0) == 0xE0) { cp = lead & 0x0F; len = 3; }
    else if ((lead & 0xF8) == 0xF0) { cp = lead & 0x07; len = 4; }
    else { ++pos; return 0xFFFD; }
    for (int i = 1; i < len; ++i) {
        if (pos + i >= text.size()) { pos += i; return 0xFFFD; }
        auto cont = static_cast<unsigned char>(text[pos + i]);
        if ((cont & 0xC0) != 0x80) { pos += i; return 0xFFFD; }
        cp = (cp << 6) | (cont & 0x3F);
    }
    pos += len;
    return cp;
}

// Encode a codepoint to UTF-8 bytes, appending to out
inline void utf8_encode(char32_t cp, std::string& out) {
    if (cp < 0x80) { out.push_back(static_cast<char>(cp)); }
    else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

struct Cell {
    char32_t ch = U' ';
    Style style;
    uint8_t width = 1;  // display width: 0 (combining), 1 (ASCII), 2 (CJK/emoji)

    bool operator==(const Cell&) const = default;
};

struct Diff {
    uint16_t x, y;
    Cell cell;
};

struct Buffer {
    std::vector<Cell> cells;
    uint16_t w = 0, h = 0;

    void resize(uint16_t width, uint16_t height) {
        w = width;
        h = height;
        cells.assign(static_cast<size_t>(w) * h, Cell{});
    }

    void clear() {
        std::fill(cells.begin(), cells.end(), Cell{});
    }

    Cell& at(uint16_t x, uint16_t y) {
        return cells[static_cast<size_t>(y) * w + x];
    }
    const Cell& at(uint16_t x, uint16_t y) const {
        return cells[static_cast<size_t>(y) * w + x];
    }

    void set_span(uint16_t x, uint16_t y, std::string_view text, Style style) {
        size_t pos = 0;
        uint16_t cx = x;
        while (pos < text.size() && cx < w) {
            size_t prev = pos;
            char32_t cp = utf8_decode(text, pos);
            if (cp == U'\n' || cp == U'\r') continue;
            uint8_t cw = char_width(cp);
            if (cw == 0 && cx > 0) {
                continue;
            }
            auto& cell = at(cx, y);
            cell.ch = cp;
            cell.style = style;
            cell.width = cw;
            cx += cw > 0 ? cw : 1;
        }
    }

    void set_line(uint16_t x, uint16_t y, std::string_view text, Style style) {
        uint16_t cy = y;
        size_t start = 0;
        for (size_t i = 0; i <= text.size(); ++i) {
            if (i == text.size() || text[i] == '\n') {
                set_span(x, cy, text.substr(start, i - start), style);
                ++cy;
                start = i + 1;
            }
        }
    }

    std::vector<Diff> diff(const Buffer& prev) const {
        std::vector<Diff> diffs;
        uint16_t max_h = std::min(h, prev.h);
        uint16_t max_w = std::min(w, prev.w);
        for (uint16_t y = 0; y < max_h; ++y) {
            for (uint16_t x = 0; x < max_w; ++x) {
                if (at(x, y) != prev.at(x, y)) {
                    diffs.push_back({x, y, at(x, y)});
                }
            }
            for (uint16_t x = max_w; x < w; ++x) {
                if (at(x, y).ch != U' ') {
                    diffs.push_back({x, y, at(x, y)});
                }
            }
        }
        for (uint16_t y = max_h; y < h; ++y) {
            for (uint16_t x = 0; x < w; ++x) {
                if (at(x, y).ch != U' ') {
                    diffs.push_back({x, y, at(x, y)});
                }
            }
        }
        return diffs;
    }
};

inline uint16_t display_width(std::string_view text) {
    uint16_t w = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        auto cp = utf8_decode(text, pos);
        w += std::max(uint8_t(1), char_width(cp));
    }
    return w;
}

struct Span {
    std::string text;
    Style style;
};

} // namespace merak::tui
