#include "theme.hpp"

namespace merak::theme {

bool is_tty() {
    return isatty(STDOUT_FILENO) != 0;
}

std::string styled(const char* ansi_code, const std::string& text) {
    if (!is_tty()) return text;
    return std::string(ansi_code) + text + ANSI_RESET;
}

std::string ok_prefix() {
    if (!is_tty()) return "  [OK] ";
    return "  " + std::string(ANSI_SUCCESS) + ICON_OK + ANSI_RESET + " ";
}

std::string err_prefix() {
    if (!is_tty()) return "  [ERR] ";
    return "  " + std::string(ANSI_ERROR) + ICON_ERR + ANSI_RESET + " ";
}

std::string warn_prefix() {
    if (!is_tty()) return "  [WARN] ";
    return "  " + std::string(ANSI_WARNING) + ICON_WARN + ANSI_RESET + " ";
}

std::string info_prefix() {
    if (!is_tty()) return "  [INFO] ";
    return "  " + std::string(ANSI_ACCENT) + ICON_INFO + ANSI_RESET + " ";
}

std::string run_prefix() {
    if (!is_tty()) return "  [...] ";
    return "  " + std::string(ANSI_DIM) + ICON_RUN + ANSI_RESET + " ";
}

} // namespace merak::theme
