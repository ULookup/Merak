#include "theme.hpp"
#include <unistd.h>

namespace merak::theme {

bool is_tty() {
    return isatty(STDOUT_FILENO) == 1;
}

std::string styled(std::string_view ansi_code, std::string_view text) {
    if (!is_tty()) return std::string(text);
    return std::string(ansi_code) + std::string(text) + ANSI_RESET;
}

std::string ok_prefix() {
    if (!is_tty()) return "  [OK] ";
    return "  " + ANSI_SUCCESS + ICON_OK + ANSI_RESET + " ";
}

std::string err_prefix() {
    if (!is_tty()) return "  [ERR] ";
    return "  " + ANSI_ERROR + ICON_ERR + ANSI_RESET + " ";
}

std::string warn_prefix() {
    if (!is_tty()) return "  [WARN] ";
    return "  " + ANSI_WARNING + ICON_WARN + ANSI_RESET + " ";
}

std::string info_prefix() {
    if (!is_tty()) return "  [INFO] ";
    return "  " + ANSI_ACCENT + ICON_INFO + ANSI_RESET + " ";
}

std::string run_prefix() {
    if (!is_tty()) return "  [...] ";
    return "  " + ANSI_DIM + ICON_RUN + ANSI_RESET + " ";
}

} // namespace merak::theme
