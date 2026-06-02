#pragma once
#include <sys/select.h>
#include <unistd.h>
#include <string>

namespace merak::tui {

struct TerminalEvent {
    enum class Type {
        None, Character, Enter, ShiftEnter, Backspace, DeleteKey, Escape, Tab,
        Up, Down, Left, Right, Home, End, F1, Paste, CtrlA, CtrlC, CtrlD, CtrlE,
        CtrlK, CtrlL, CtrlO, CtrlT, CtrlU, CtrlW, CtrlY,
    };
    Type type = Type::None;
    char character = 0;
    std::string text;
};

class TerminalEventReader {
    static bool read_byte(char& c, int timeout_ms) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
        if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) <= 0) return false;
        return ::read(STDIN_FILENO, &c, 1) == 1;
    }

    static std::string read_escape_tail() {
        std::string tail;
        char c;
        while (read_byte(c, 2)) {
            tail.push_back(c);
            if ((c >= 'A' && c <= 'Z') || c == '~') break;
        }
        return tail;
    }

public:
    TerminalEvent next(int timeout_ms = 100) const {
        char c;
        if (!read_byte(c, timeout_ms)) return {};
        if (c == '\x1b') {
            auto tail = read_escape_tail();
            if (tail == "O" && read_byte(c, 2)) tail.push_back(c);
            if (tail == "[200~") {
                std::string paste;
                std::string marker;
                while (read_byte(c, 1000)) {
                    marker.push_back(c);
                    if (marker.ends_with("\x1b[201~")) {
                        paste += marker.substr(0, marker.size() - 6);
                        break;
                    }
                    if (marker.size() > 6) {
                        paste.push_back(marker.front());
                        marker.erase(marker.begin());
                    }
                }
                return {TerminalEvent::Type::Paste, 0, std::move(paste)};
            }
            if (tail == "[A") return {TerminalEvent::Type::Up};
            if (tail == "[B") return {TerminalEvent::Type::Down};
            if (tail == "[C") return {TerminalEvent::Type::Right};
            if (tail == "[D") return {TerminalEvent::Type::Left};
            if (tail == "[H" || tail == "[1~") return {TerminalEvent::Type::Home};
            if (tail == "[F" || tail == "[4~") return {TerminalEvent::Type::End};
            if (tail == "[3~") return {TerminalEvent::Type::DeleteKey};
            if (tail == "OP") return {TerminalEvent::Type::F1};
            if (tail == "\r" || tail == "\n") return {TerminalEvent::Type::ShiftEnter};
            if (tail.empty()) return {TerminalEvent::Type::Escape};
            return {};
        }
        switch (c) {
            case '\r': case '\n': return {TerminalEvent::Type::Enter};
            case '\x7f': case '\x08': return {TerminalEvent::Type::Backspace};
            case '\t': return {TerminalEvent::Type::Tab};
            case '\x01': return {TerminalEvent::Type::CtrlA};
            case '\x03': return {TerminalEvent::Type::CtrlC};
            case '\x04': return {TerminalEvent::Type::CtrlD};
            case '\x05': return {TerminalEvent::Type::CtrlE};
            case '\x0b': return {TerminalEvent::Type::CtrlK};
            case '\x0c': return {TerminalEvent::Type::CtrlL};
            case '\x0f': return {TerminalEvent::Type::CtrlO};
            case '\x14': return {TerminalEvent::Type::CtrlT};
            case '\x15': return {TerminalEvent::Type::CtrlU};
            case '\x17': return {TerminalEvent::Type::CtrlW};
            case '\x19': return {TerminalEvent::Type::CtrlY};
            default:
                if (static_cast<unsigned char>(c) >= 0x20) {
                    return {TerminalEvent::Type::Character, c};
                }
                return {};
        }
    }
};

} // namespace merak::tui
