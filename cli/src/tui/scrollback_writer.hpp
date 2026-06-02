#pragma once
#include "history_cell/history_cell.hpp"
#include <iostream>
#include <string>
#include <vector>

namespace merak::tui {

class ScrollbackWriter {
public:
    static void write(const std::vector<std::string>& lines) {
        for (const auto& line : lines) {
            std::cout << line << "\r\n";
        }
        std::cout.flush();
    }
};

} // namespace merak::tui
