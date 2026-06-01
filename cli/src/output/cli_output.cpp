#include "cli_output.hpp"
#include "../theme/theme.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>

namespace {

std::string repeat_char(const char* s, size_t n) {
    std::string out;
    out.reserve(std::char_traits<char>::length(s) * n);
    for (size_t i = 0; i < n; i++) out += s;
    return out;
}

} // anonymous namespace

namespace merak::cli {

using namespace theme;

void ok(const std::string& msg) {
    std::cerr << ok_prefix() << msg << "\n";
}

void warn(const std::string& msg) {
    std::cerr << warn_prefix() << msg << "\n";
}

void err(const std::string& msg) {
    std::cerr << err_prefix() << msg << "\n";
}

void info(const std::string& msg) {
    std::cerr << info_prefix() << msg << "\n";
}

void section(const std::string& title) {
    std::cerr << "\n";
    std::cerr << styled(ANSI_BOLD, styled(ANSI_ACCENT, "=== " + title + " ===")) << "\n";
}

void dim(const std::string& msg) {
    std::cerr << styled(ANSI_DIM, "     " + msg) << "\n";
}

void rule() {
    std::cerr << styled(ANSI_BORDER, repeat_char("─", 40)) << "\n";
}

void kv(const std::string& label, const std::string& value) {
    std::cerr << "  " << styled(ANSI_DIM, label + ":") << " " << value << "\n";
}

void bullet(const std::string& text) {
    std::cerr << "  " << styled(ANSI_DIM, "-") << " " << text << "\n";
}

void numbered(int index, const std::string& text) {
    std::ostringstream ss;
    ss << std::setw(2) << index << ".";
    std::cerr << "  " << styled(ANSI_DIM, ss.str()) << " " << text << "\n";
}

void table(const std::vector<std::string>& headers,
           const std::vector<std::vector<std::string>>& rows) {
    if (headers.empty() || rows.empty()) return;

    std::vector<size_t> widths(headers.size(), 0);
    for (size_t i = 0; i < headers.size(); i++) {
        widths[i] = headers[i].size();
    }
    for (auto& row : rows) {
        for (size_t i = 0; i < row.size() && i < widths.size(); i++) {
            widths[i] = std::max(widths[i], row[i].size());
        }
    }

    std::ostringstream header_line;
    for (size_t i = 0; i < headers.size(); i++) {
        header_line << std::setw(static_cast<int>(widths[i])) << std::left << headers[i];
        if (i < headers.size() - 1) header_line << "  ";
    }
    std::cerr << "  " << styled(ANSI_BOLD, header_line.str()) << "\n";

    std::ostringstream sep;
    for (size_t i = 0; i < headers.size(); i++) {
        sep << repeat_char("─", widths[i]);
        if (i < headers.size() - 1) sep << "──";
    }
    std::cerr << "  " << styled(ANSI_DIM, sep.str()) << "\n";

    for (auto& row : rows) {
        std::ostringstream row_line;
        for (size_t i = 0; i < row.size() && i < widths.size(); i++) {
            row_line << std::setw(static_cast<int>(widths[i])) << std::left << row[i];
            if (i < widths.size() - 1) row_line << "  ";
        }
        std::cerr << "  " << row_line.str() << "\n";
    }
}

std::string duration_ms(uint64_t ms) {
    if (ms < 1000) return std::to_string(ms) + "ms";
    uint64_t secs = ms / 1000;
    if (secs < 60) {
        uint64_t frac = (ms % 1000) / 100;
        if (frac > 0) return std::to_string(secs) + "." + std::to_string(frac) + "s";
        return std::to_string(secs) + "s";
    }
    uint64_t m = secs / 60;
    uint64_t s = secs % 60;
    if (s > 0) return std::to_string(m) + "m " + std::to_string(s) + "s";
    return std::to_string(m) + "m";
}

std::string byte_size(size_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + "B";
    if (bytes < 1024 * 1024) {
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(1) << (bytes / 1024.0) << "KB";
        return ss.str();
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1) << (bytes / (1024.0 * 1024.0)) << "MB";
    return ss.str();
}

std::string truncate_path(const std::string& path, size_t max_chars) {
    if (path.size() <= max_chars) return path;
    size_t keep = max_chars - 4;
    size_t slash = path.rfind('/');
    if (slash != std::string::npos) {
        std::string tail = path.substr(slash);
        if (tail.size() < keep) {
            return "..." + path.substr(path.size() - keep);
        }
    }
    return path.substr(0, max_chars - 1) + "…";
}

} // namespace merak::cli
