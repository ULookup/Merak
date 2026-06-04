#pragma once
#include "render_frame.hpp"
#include "scrollback_writer.hpp"
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <iostream>
#include <functional>
#include <string>
#include <vector>

namespace merak::tui {

class InlineTerminal {
    termios original_{};
    bool raw_ = false;
    size_t viewport_height_ = 0;

    void clear_viewport() {
        if (viewport_height_ > 1) {
            std::cout << "\x1b[" << (viewport_height_ - 1) << "A";
        }
        std::cout << "\r\x1b[J";
    }

public:
    InlineTerminal() {
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

    ~InlineTerminal() {
        clear_viewport();
        if (raw_) tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
        std::cout << "\x1b[?2004l\x1b[?25h\r\n" << std::flush;
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

    void redraw(const std::vector<std::string>& lines) {
        clear_viewport();
        viewport_height_ = lines.empty() ? 1 : lines.size();
        std::cout << join_frame(lines) << std::flush;
    }

    void flush_scrollback(const std::vector<std::string>& lines) {
        if (lines.empty()) return;
        clear_viewport();
        viewport_height_ = 0;
        ScrollbackWriter::write(lines);
    }

    void invalidate() { viewport_height_ = 0; }
};

} // namespace merak::tui
