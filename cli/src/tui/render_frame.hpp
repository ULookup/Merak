#pragma once
#include <string>
#include <vector>

namespace merak::tui {

inline std::string join_frame(const std::vector<std::string>& lines) {
    std::string output;
    for (size_t i = 0; i < lines.size(); ++i) {
        output += lines[i];
        output += "\x1b[K";
        if (i + 1 < lines.size()) output += "\r\n";
    }
    return output;
}

} // namespace merak::tui
