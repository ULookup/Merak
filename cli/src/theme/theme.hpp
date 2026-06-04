#pragma once
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace merak::theme {

struct Theme {
    int fg = 252;
    int dim = 101;
    int faint = 58;
    int border = 58;
    int accent = 178;
    int selected_bg = 236;
    int success = 71;
    int warn = 178;
    int error = 160;
    int gutter = 178;
    int link = 68;
};

inline std::string fg(int color) { return "\x1b[38;5;" + std::to_string(color) + "m"; }
inline std::string bg(int color) { return "\x1b[48;5;" + std::to_string(color) + "m"; }

inline bool terminal_background_is_light(const char* value = std::getenv("COLORFGBG")) {
    if (!value) return false;
    std::string text(value);
    auto pos = text.rfind(';');
    auto last = pos == std::string::npos ? text : text.substr(pos + 1);
    try {
        auto bg = std::stoi(last);
        return bg >= 0 && bg <= 6;
    } catch (...) {
        return false;
    }
}

inline int parse_color_value(const nlohmann::json& value, int fallback) {
    static const std::map<std::string, int> ansi16 = {
        {"black", 0}, {"red", 1}, {"green", 2}, {"yellow", 3}, {"blue", 4},
        {"magenta", 5}, {"cyan", 6}, {"white", 7}, {"bright_black", 8},
        {"bright_red", 9}, {"bright_green", 10}, {"bright_yellow", 11},
        {"bright_blue", 12}, {"bright_magenta", 13}, {"bright_cyan", 14},
        {"bright_white", 15},
    };
    if (value.is_number_integer()) {
        auto color = value.get<int>();
        return color >= 0 && color <= 255 ? color : fallback;
    }
    if (!value.is_string()) return fallback;
    auto text = value.get<std::string>();
    if (auto it = ansi16.find(text); it != ansi16.end()) return it->second;
    try {
        auto color = std::stoi(text);
        return color >= 0 && color <= 255 ? color : fallback;
    } catch (...) {
        return fallback;
    }
}

inline std::filesystem::path theme_path() {
    if (auto* home = std::getenv("HOME")) return std::filesystem::path(home) / ".merak" / "theme.json";
    return std::filesystem::path(".merak") / "theme.json";
}

inline Theme load_theme(std::optional<std::filesystem::path> path = std::nullopt) {
    Theme theme;
    if (terminal_background_is_light()) {
        theme.fg = 16;
        theme.dim = 244;
        theme.faint = 250;
        theme.border = 250;
        theme.accent = 25;
        theme.selected_bg = 254;
        theme.success = 28;
        theme.warn = 136;
        theme.error = 160;
        theme.gutter = 25;
        theme.link = 27;
    }
    std::ifstream input(path.value_or(theme_path()));
    if (!input) return theme;
    try {
        auto json = nlohmann::json::parse(input);
        auto apply = [&](const char* key, int& slot) {
            if (json.contains(key)) slot = parse_color_value(json[key], slot);
        };
        apply("fg", theme.fg);
        apply("dim", theme.dim);
        apply("faint", theme.faint);
        apply("border", theme.border);
        apply("accent", theme.accent);
        apply("selected_bg", theme.selected_bg);
        apply("success", theme.success);
        apply("warn", theme.warn);
        apply("error", theme.error);
        apply("gutter", theme.gutter);
        apply("link", theme.link);
    } catch (...) {
    }
    return theme;
}

inline const Theme& active_theme() {
    static const Theme theme = load_theme();
    return theme;
}

inline const std::string ANSI_FG      = fg(active_theme().fg);
inline const std::string ANSI_ACCENT  = fg(active_theme().accent);
inline const std::string ANSI_DIM     = fg(active_theme().dim);
inline const std::string ANSI_FAINT   = fg(active_theme().faint);
inline const std::string ANSI_BORDER  = fg(active_theme().border);
inline const std::string ANSI_ERROR   = fg(active_theme().error);
inline const std::string ANSI_SUCCESS = fg(active_theme().success);
inline const std::string ANSI_WARNING = fg(active_theme().warn);
inline const std::string ANSI_LINK    = fg(active_theme().link);
inline const std::string ANSI_GUTTER  = fg(active_theme().gutter);
inline const std::string ANSI_SELECTED_BG = bg(active_theme().selected_bg);

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
std::string styled(std::string_view ansi_code, std::string_view text);
std::string ok_prefix();
std::string err_prefix();
std::string warn_prefix();
std::string info_prefix();
std::string run_prefix();

} // namespace merak::theme
