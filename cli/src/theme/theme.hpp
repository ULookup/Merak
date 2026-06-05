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
    int selected_fg = 252;
    int success = 71;
    int warn = 178;
    int error = 160;
    int gutter = 178;
    int gutter_frozen = 141;
    int link = 68;
    int quote = 71;
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
    if (text == "default" || text == "reset") return fallback;
    if (auto it = ansi16.find(text); it != ansi16.end()) return it->second;
    try {
        auto color = std::stoi(text);
        return color >= 0 && color <= 255 ? color : fallback;
    } catch (...) {
        return fallback;
    }
}

inline Theme dark_theme() {
    Theme theme;
    theme.fg = 252;
    theme.dim = 101;
    theme.faint = 58;
    theme.border = 58;
    theme.accent = 178;
    theme.selected_bg = 236;
    theme.selected_fg = 252;
    theme.success = 71;
    theme.warn = 178;
    theme.error = 160;
    theme.gutter = 178;
    theme.gutter_frozen = 141;
    theme.link = 68;
    theme.quote = 71;
    return theme;
}

inline Theme light_theme() {
    Theme theme;
    theme.fg = 16;
    theme.dim = 244;
    theme.faint = 250;
    theme.border = 250;
    theme.accent = 25;
    theme.selected_bg = 254;
    theme.selected_fg = 16;
    theme.success = 28;
    theme.warn = 136;
    theme.error = 160;
    theme.gutter = 25;
    theme.gutter_frozen = 61;
    theme.link = 27;
    theme.quote = 28;
    return theme;
}

inline void apply_theme_json(Theme& theme, const nlohmann::json& json) {
    auto apply = [&](const char* key, int& slot) {
        if (json.contains(key)) slot = parse_color_value(json[key], slot);
    };
    apply("fg", theme.fg);
    apply("dim", theme.dim);
    apply("faint", theme.faint);
    apply("border", theme.border);
    apply("accent", theme.accent);
    apply("selected_bg", theme.selected_bg);
    apply("selected_fg", theme.selected_fg);
    apply("success", theme.success);
    apply("warn", theme.warn);
    apply("error", theme.error);
    apply("gutter", theme.gutter);
    apply("gutter_frozen", theme.gutter_frozen);
    apply("link", theme.link);
    apply("quote", theme.quote);
    if (json.contains("colors") && json["colors"].is_object()) {
        apply_theme_json(theme, json["colors"]);
    }
}

inline Theme theme_for_preset(std::string preset) {
    if (preset == "light") return light_theme();
    if (preset == "dark") return dark_theme();
    return terminal_background_is_light() ? light_theme() : dark_theme();
}

inline std::filesystem::path theme_path() {
    if (auto* home = std::getenv("HOME")) return std::filesystem::path(home) / ".merak" / "theme.json";
    return std::filesystem::path(".merak") / "theme.json";
}

inline Theme load_theme(std::optional<std::filesystem::path> path = std::nullopt) {
    Theme theme = theme_for_preset("auto");
    std::ifstream input(path.value_or(theme_path()));
    if (!input) return theme;
    try {
        auto json = nlohmann::json::parse(input);
        theme = theme_for_preset(json.value("preset", "auto"));
        apply_theme_json(theme, json);
    } catch (...) {
    }
    return theme;
}

inline Theme load_theme_from_metadata(const nlohmann::json& metadata) {
    Theme theme = load_theme();
    if (!metadata.is_object()) return theme;
    auto preset = metadata.value("preset", std::string("auto"));
    theme = theme_for_preset(preset);
    apply_theme_json(theme, metadata);
    return theme;
}

inline const Theme& active_theme() {
    static Theme theme = load_theme();
    return theme;
}

inline std::string ANSI_FG      = fg(active_theme().fg);
inline std::string ANSI_ACCENT  = fg(active_theme().accent);
inline std::string ANSI_DIM     = fg(active_theme().dim);
inline std::string ANSI_FAINT   = fg(active_theme().faint);
inline std::string ANSI_BORDER  = fg(active_theme().border);
inline std::string ANSI_ERROR   = fg(active_theme().error);
inline std::string ANSI_SUCCESS = fg(active_theme().success);
inline std::string ANSI_WARNING = fg(active_theme().warn);
inline std::string ANSI_LINK    = fg(active_theme().link);
inline std::string ANSI_GUTTER  = fg(active_theme().gutter);
inline std::string ANSI_SELECTED_BG = bg(active_theme().selected_bg);

inline void refresh_ansi_strings() {
    ANSI_FG = fg(active_theme().fg);
    ANSI_ACCENT = fg(active_theme().accent);
    ANSI_DIM = fg(active_theme().dim);
    ANSI_FAINT = fg(active_theme().faint);
    ANSI_BORDER = fg(active_theme().border);
    ANSI_ERROR = fg(active_theme().error);
    ANSI_SUCCESS = fg(active_theme().success);
    ANSI_WARNING = fg(active_theme().warn);
    ANSI_LINK = fg(active_theme().link);
    ANSI_GUTTER = fg(active_theme().gutter);
    ANSI_SELECTED_BG = bg(active_theme().selected_bg);
}

inline void configure_theme(Theme theme) {
    const_cast<Theme&>(active_theme()) = theme;
    refresh_ansi_strings();
}

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
