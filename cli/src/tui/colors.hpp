#pragma once
#include "../theme/theme.hpp"
#include <ftxui/screen/color.hpp>

namespace merak::tui::colors {

inline const ftxui::Color accent = ftxui::Color::Palette256(::merak::theme::active_theme().accent);
inline const ftxui::Color light_gold = ftxui::Color::Palette256(180);
inline const ftxui::Color muted = ftxui::Color::Palette256(::merak::theme::active_theme().dim);
inline const ftxui::Color info = ftxui::Color::Palette256(::merak::theme::active_theme().link);
inline const ftxui::Color success = ftxui::Color::Palette256(::merak::theme::active_theme().success);
inline const ftxui::Color error = ftxui::Color::Palette256(::merak::theme::active_theme().error);
inline const ftxui::Color white = ftxui::Color::White;

} // namespace merak::tui::colors
