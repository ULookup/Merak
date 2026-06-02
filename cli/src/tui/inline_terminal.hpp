#pragma once
#include "history_cell.hpp"
#include <ftxui/component/screen_interactive.hpp>
#include <iostream>
#include <memory>
#include <vector>

namespace merak::tui {

inline void flush_scrollback(
    ftxui::ScreenInteractive& screen,
    std::vector<std::unique_ptr<HistoryCell>> cells
) {
    if (cells.empty()) return;
    std::vector<std::string> lines;
    for (const auto& cell : cells) {
        for (const auto& line : cell->lines()) lines.push_back(line);
        lines.emplace_back();
    }
    auto write = screen.WithRestoredIO([lines = std::move(lines)]() {
        for (const auto& line : lines) std::cout << line << '\n';
        std::cout << std::flush;
    });
    write();
}

} // namespace merak::tui
