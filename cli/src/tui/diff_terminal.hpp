#pragma once
#include "buffer.hpp"
#include <algorithm>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <iostream>
#include <functional>
#include <string>
#include <vector>

namespace merak::tui {

class DiffTerminal {
    termios original_{};
    bool raw_ = false;
    uint16_t viewport_top_ = 0;
    uint16_t viewport_height_ = 0;
    uint16_t viewport_width_ = 0;
    Buffer prev_buffer_;

    static void write_style(const Style& style, const Style& prev) {
        if (style == prev) return;
        std::cout << "\x1b[0m";
        if (style.fg != 252) std::cout << "\x1b[38;5;" << static_cast<int>(style.fg) << "m";
        if (style.bg != 255) std::cout << "\x1b[48;5;" << static_cast<int>(style.bg) << "m";
        if (style.bold())      std::cout << "\x1b[1m";
        if (style.dim())       std::cout << "\x1b[2m";
        if (style.italic())    std::cout << "\x1b[3m";
        if (style.underline()) std::cout << "\x1b[4m";
    }

    static void move_absolute(uint16_t x, uint16_t y) {
        std::cout << "\x1b[" << (y + 1) << ";" << (x + 1) << "H";
    }

    void move_cursor(uint16_t x, uint16_t y) {
        move_absolute(x, static_cast<uint16_t>(viewport_top_ + y));
    }

    void clear_viewport() {
        if (viewport_height_ == 0) return;
        move_absolute(0, viewport_top_);
        std::cout << "\x1b[J";
    }

    void update_viewport(uint16_t height) {
        const auto screen_h = static_cast<uint16_t>(this->height());
        const auto screen_w = static_cast<uint16_t>(this->width());
        const auto next_height = std::min<uint16_t>(height, screen_h);
        const auto next_top = static_cast<uint16_t>(screen_h - next_height);
        if (next_height == viewport_height_ && next_top == viewport_top_
            && screen_w == viewport_width_) {
            return;
        }

        if (viewport_height_ > 0) {
            const auto clear_top = std::min(viewport_top_, next_top);
            move_absolute(0, clear_top);
            std::cout << "\x1b[J";
        }
        viewport_top_ = next_top;
        viewport_height_ = next_height;
        viewport_width_ = screen_w;
        prev_buffer_ = Buffer{};
    }

    bool insert_history_lines(const std::vector<std::string>& lines) {
        if (lines.empty()) return true;
        if (viewport_top_ == 0) return false;

        const auto region_bottom = viewport_top_;
        std::cout << "\x1b[1;" << region_bottom << "r";
        move_absolute(0, static_cast<uint16_t>(viewport_top_ - 1));
        for (const auto& line : lines) {
            std::cout << "\r\n\x1b[K" << line << "\x1b[0m";
        }
        std::cout << "\x1b[r";
        move_absolute(0, viewport_top_);
        std::cout.flush();
        return true;
    }

public:
    DiffTerminal() {
        if (tcgetattr(STDIN_FILENO, &original_) != 0) return;
        auto raw = original_;
        raw.c_lflag &= static_cast<unsigned long>(~(ECHO | ICANON | IEXTEN | ISIG));
        raw.c_iflag &= static_cast<unsigned long>(~(IXON | ICRNL | BRKINT | INPCK | ISTRIP));
        raw.c_oflag &= static_cast<unsigned long>(~OPOST);
        raw.c_cflag |= CS8;
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 1;
        raw_ = tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0;
        std::cout << "\x1b[?2004h\x1b[?25l";
    }

    ~DiffTerminal() {
        clear_viewport();
        if (viewport_height_ > 0) {
            move_absolute(0, static_cast<uint16_t>(viewport_top_ + viewport_height_ - 1));
        }
        if (raw_) tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
        std::cout << "\x1b[r\x1b[?2004l\x1b[?25h\r\n" << std::flush;
    }

    void with_cooked_terminal(const std::function<void()>& fn) {
        std::cout << "\x1b[?25h" << std::flush;
        if (raw_) tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
        fn();
        if (raw_) {
            auto raw = original_;
            raw.c_lflag &= static_cast<unsigned long>(~(ECHO | ICANON | IEXTEN | ISIG));
            raw.c_iflag &= static_cast<unsigned long>(~(IXON | ICRNL | BRKINT | INPCK | ISTRIP));
            raw.c_oflag &= static_cast<unsigned long>(~OPOST);
            raw.c_cflag |= CS8;
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 1;
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        }
        std::cout << "\x1b[?25l" << std::flush;
        invalidate();
    }

    size_t width() const {
        winsize size{};
        return ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0
            ? size.ws_col : 80;
    }
    size_t height() const {
        winsize size{};
        return ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row > 0
            ? size.ws_row : 24;
    }

    void draw(Buffer& curr) {
        update_viewport(curr.h);
        if (viewport_height_ == 0) return;

        auto diffs = curr.diff(prev_buffer_);
        if (diffs.empty()) return;

        Style curr_style{};
        uint16_t prev_y = UINT16_MAX;
        uint16_t run_start_x = 0;
        uint16_t run_end_x = 0;
        Style run_style{};

        auto flush_run = [&](uint16_t y) {
            if (run_start_x > run_end_x) return;
            write_style(run_style, curr_style);
            curr_style = run_style;
            move_cursor(run_start_x, y);
            std::string out;
            for (uint16_t x = run_start_x; x <= run_end_x; ++x) {
                utf8_encode(curr.at(x, y).ch, out);
            }
            std::cout << out << "\x1b[K";
        };

        for (const auto& d : diffs) {
            bool same_row = (d.y == prev_y);
            bool adjacent = same_row && (d.x == run_end_x + 1);
            bool same_style = (d.cell.style == run_style);

            if (!same_row || !adjacent || !same_style) {
                if (prev_y != UINT16_MAX) flush_run(prev_y);
                prev_y = d.y;
                run_start_x = d.x;
                run_end_x = d.x;
                run_style = d.cell.style;
            } else {
                run_end_x = d.x;
            }
        }

        if (prev_y != UINT16_MAX) flush_run(prev_y);

        if (curr.h < prev_buffer_.h) {
            for (uint16_t y = curr.h; y < prev_buffer_.h; ++y) {
                move_cursor(0, y);
                std::cout << "\x1b[K";
            }
            std::cout << "\x1b[" << (prev_buffer_.h - curr.h) << "A";
        }
        if (curr.w < prev_buffer_.w) {
            for (uint16_t y = 0; y < curr.h; ++y) {
                move_cursor(curr.w, y);
                std::cout << "\x1b[K";
            }
        }

        std::cout << "\x1b[0m" << std::flush;
        prev_buffer_ = std::move(curr);
    }

    bool flush_scrollback(const std::vector<std::string>& lines, uint16_t viewport_height) {
        if (lines.empty()) return true;
        update_viewport(viewport_height);
        const auto inserted = insert_history_lines(lines);
        if (!inserted) return false;
        prev_buffer_ = Buffer{};
        return true;
    }

    void invalidate() {
        clear_viewport();
        viewport_height_ = 0;
        prev_buffer_ = Buffer{};
    }

    void place_cursor(uint16_t x, uint16_t y) {
        move_cursor(x, y);
        std::cout.flush();
    }
};

} // namespace merak::tui
