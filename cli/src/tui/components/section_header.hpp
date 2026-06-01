#pragma once
#include <string>
#include <ftxui/dom/elements.hpp>

namespace merak::tui {

inline ftxui::Element section_header(const std::string& title) {
    using namespace ftxui;
    return text(" " + title + " ") | bold | color(Color::Palette256(178));
}

} // namespace merak::tui
