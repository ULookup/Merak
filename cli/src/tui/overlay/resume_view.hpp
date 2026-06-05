#pragma once
#include "../buffer.hpp"
#include "../persistence/transcript.hpp"
#include "../../theme/theme.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <sstream>
#include <vector>
#include <string>

namespace merak::tui {

struct SessionEntry {
    std::string sid;
    uint64_t ts = 0;
    std::string model;
    int msg_count = 0;
    std::string cwd;
};

class ResumeView {
    std::vector<SessionEntry> entries_;
    int selected_ = 0;
    std::string filter_;
    int scroll_ = 0;
    bool show_preview_ = false;

    void load_index() {
        auto path = persistence::index_path();
        if (!std::filesystem::exists(path)) return;
        std::ifstream in(path);
        nlohmann::json index;
        try { in >> index; } catch (...) { return; }
        entries_.clear();
        for (const auto& e : index) {
            entries_.push_back({
                e.value("sid", ""),
                e.value("ts", 0ULL),
                e.value("model", ""),
                e.value("msg_count", 0),
                e.value("cwd", "")
            });
        }
        std::sort(entries_.begin(), entries_.end(),
            [](const auto& a, const auto& b) { return a.ts > b.ts; });
    }

    std::vector<SessionEntry> filtered() const {
        if (filter_.empty()) return entries_;
        std::vector<SessionEntry> result;
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            return s;
        };
        auto f = lower(filter_);
        for (const auto& e : entries_) {
            if (lower(e.sid).find(f) != std::string::npos
                || lower(e.model).find(f) != std::string::npos
                || lower(e.cwd).find(f) != std::string::npos) {
                result.push_back(e);
            }
        }
        return result;
    }

public:
    ResumeView() { load_index(); }

    void handle_key(const std::string& key) {
        if (key == "up" && selected_ > 0) --selected_;
        if (key == "down" && selected_ + 1 < static_cast<int>(filtered().size())) ++selected_;
        if (key == "ctrl+r") show_preview_ = !show_preview_;
    }

    void set_filter(const std::string& f) { filter_ = f; selected_ = 0; }
    const std::string& get_filter() const { return filter_; }
    int selected() const { return selected_; }
    std::string selected_sid() const {
        auto f = filtered();
        return (selected_ >= 0 && selected_ < static_cast<int>(f.size()))
            ? f[selected_].sid : "";
    }

    void render(Buffer& buf, uint16_t width) const {
        auto f = filtered();
        uint16_t y = 0;

        Style accent; accent.fg = theme::active_theme().accent; accent.bold(true);
        buf.set_span(0, y, "Resume Session", accent); ++y;

        Style dim; dim.fg = theme::active_theme().dim; dim.dim(true);
        buf.set_span(0, y, "/ " + filter_, dim); ++y;

        buf.set_span(0, y, std::string(width, '─'), dim); ++y;

        int start = std::max(0, selected_ - static_cast<int>(buf.h) + 8);
        for (size_t i = start; i < f.size() && y < buf.h - 2; ++i, ++y) {
            bool sel = (static_cast<int>(i) == selected_);
            Style line_style;
            if (sel) { line_style.fg = theme::active_theme().accent; line_style.bold(true); }

            std::string prefix = sel ? "▶ " : "  ";
            std::ostringstream oss;
            oss << f[i].sid << "  " << f[i].model << "  " << f[i].msg_count << " msgs";
            buf.set_span(0, y, prefix + oss.str(), line_style);
            ++y;
            if (y < buf.h) {
                Style cwd_style; cwd_style.fg = theme::active_theme().dim;
                buf.set_span(4, y, f[i].cwd, cwd_style);
            }
        }

        if (y < buf.h) {
            buf.set_span(0, buf.h - 1,
                "↑↓ select  Enter resume  / search  Ctrl+R preview  Ctrl+D delete  Esc back", dim);
        }
    }
};

} // namespace merak::tui
