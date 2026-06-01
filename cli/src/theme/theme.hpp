#pragma once
#include <string>
#include <string_view>

namespace merak::theme {

// ── Main palette — low-saturation yellow ──
constexpr const char* ANSI_ACCENT  = "\x1b[38;5;178m";  // #D4A017
constexpr const char* ANSI_DIM     = "\x1b[38;5;101m";  // #908050
constexpr const char* ANSI_FAINT   = "\x1b[38;5;58m";   // #706830
constexpr const char* ANSI_BORDER  = "\x1b[38;5;58m";   // #504820

// ── Semantic status colors ──
constexpr const char* ANSI_ERROR   = "\x1b[38;5;160m";  // #CC3333 red
constexpr const char* ANSI_SUCCESS = "\x1b[38;5;71m";   // #6AAA44 green
constexpr const char* ANSI_WARNING = "\x1b[38;5;68m";   // #4488CC blue

// ── Text styles ──
constexpr const char* ANSI_BOLD    = "\x1b[1m";
constexpr const char* ANSI_RESET   = "\x1b[0m";

// ── Unicode symbols (no emoji) ──
constexpr const char* ICON_OK      = "●";
constexpr const char* ICON_ERR     = "✗";
constexpr const char* ICON_WARN    = "⚠";
constexpr const char* ICON_INFO    = "ℹ";
constexpr const char* ICON_RUN     = "○";

// ── Functions ──
bool is_tty();
bool supports_tui();
std::string styled(const char* ansi_code, std::string_view text);
std::string ok_prefix();
std::string err_prefix();
std::string warn_prefix();
std::string info_prefix();
std::string run_prefix();

} // namespace merak::theme
