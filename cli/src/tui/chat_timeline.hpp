#pragma once
#include "history_cell/history_cell.hpp"
#include "buffer.hpp"
#include <algorithm>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace merak::tui {

inline std::vector<std::string> buffer_to_lines(const Buffer& buf) {
    std::vector<std::string> lines;
    auto height = buf.h;
    while (height > 0) {
        bool empty = true;
        for (uint16_t x = 0; x < buf.w; ++x) {
            if (!buf.is_blank(x, static_cast<uint16_t>(height - 1))) {
                empty = false;
                break;
            }
        }
        if (!empty) break;
        --height;
    }
    for (uint16_t y = 0; y < height; ++y) {
        std::string line;
        Style curr;
        auto line_width = buf.w;
        while (line_width > 0 && buf.is_blank(static_cast<uint16_t>(line_width - 1), y)) {
            --line_width;
        }
        for (uint16_t x = 0; x < line_width; ++x) {
            const auto& cell = buf.at(x, y);
            if (cell.width == 0) continue;
            if (cell.ch == U' ') { line.push_back(' '); continue; }
            if (!(cell.style == curr)) {
                line += "\x1b[0m";
                if (cell.style.fg != 252) line += "\x1b[38;5;" + std::to_string(cell.style.fg) + "m";
                if (cell.style.bg != 255) line += "\x1b[48;5;" + std::to_string(cell.style.bg) + "m";
                if (cell.style.bold()) line += "\x1b[1m";
                if (cell.style.dim()) line += "\x1b[2m";
                if (cell.style.italic()) line += "\x1b[3m";
                if (cell.style.underline()) line += "\x1b[4m";
                curr = cell.style;
            }
            utf8_encode(cell.ch, line);
        }
        line += "\x1b[0m";
        lines.push_back(line);
    }
    return lines;
}

class ChatTimeline {
    std::vector<std::shared_ptr<HistoryCell>> committed_;
    std::shared_ptr<HistoryCell> active_;
    std::map<std::string, std::shared_ptr<ToolCell>> tools_by_id_;
    std::vector<std::shared_ptr<ToolCell>> tools_;
    size_t scrollback_watermark_ = 0;

    static uint16_t content_height(const Buffer& buf) {
        auto height = buf.h;
        while (height > 0) {
            bool empty = true;
            for (uint16_t x = 0; x < buf.w; ++x) {
                if (!buf.is_blank(x, static_cast<uint16_t>(height - 1))) {
                    empty = false;
                    break;
                }
            }
            if (!empty) break;
            --height;
        }
        return height;
    }

    static Buffer render_cell(const HistoryCell& cell, uint16_t width) {
        static constexpr uint16_t kMaxCellHeight = 80;
        Buffer cell_buf;
        cell_buf.resize(width, kMaxCellHeight);
        cell.render(cell_buf, width);
        auto height = content_height(cell_buf);
        if (height != cell_buf.h) {
            Buffer trimmed;
            trimmed.resize(width, height);
            for (uint16_t y = 0; y < height; ++y) {
                for (uint16_t x = 0; x < width; ++x) {
                    trimmed.at(x, y) = cell_buf.at(x, y);
                }
            }
            return trimmed;
        }
        return cell_buf;
    }

public:
    const std::vector<std::shared_ptr<HistoryCell>>& committed() const { return committed_; }
    const std::vector<std::shared_ptr<ToolCell>>& tools() const { return tools_; }
    const std::shared_ptr<HistoryCell>& active() const { return active_; }
    void clear() { committed_.clear(); active_.reset(); tools_by_id_.clear(); tools_.clear(); scrollback_watermark_=0; }

    void commit(std::shared_ptr<HistoryCell> cell) {
        cell->finalize();
        committed_.push_back(cell);
    }

    void submit_user(std::string text) { commit(std::make_shared<UserCell>(std::move(text))); }
    void add_system(std::string text, bool error = false) {
        commit(std::make_shared<SystemCell>(std::move(text), error));
    }
    void add_summary(long elapsed_ms, int input, int output, int tools,
                     int cumulative, bool has_usage) {
        commit(std::make_shared<TurnSummaryCell>(
            elapsed_ms, input, output, tools, cumulative, has_usage));
    }

    void append_assistant(std::string_view delta) {
        auto assistant = std::dynamic_pointer_cast<AssistantCell>(active_);
        if (!assistant) {
            commit_active();
            assistant = std::make_shared<AssistantCell>();
            active_ = assistant;
        }
        assistant->append(delta);
    }

    void start_tool(ToolCall call) {
        commit_active();
        auto cell = std::make_shared<ToolCell>(std::move(call));
        tools_by_id_[cell->call().id] = cell;
        tools_.push_back(cell);
        active_ = cell;
    }

    void finish_tool(const ToolResult& result) {
        auto it = tools_by_id_.find(result.call_id);
        if (it == tools_by_id_.end()) return;
        it->second->complete(result);
        if (active_ == it->second) commit_active();
        tools_by_id_.erase(it);
    }

    void commit_active() {
        if (!active_) return;
        auto cell = std::move(active_);
        active_.reset();
        commit(std::move(cell));
    }

    std::vector<std::string> pending_scrollback(size_t width) const {
        std::vector<std::string> lines;
        for (size_t i = scrollback_watermark_; i < committed_.size(); ++i) {
            auto cell_buf = render_cell(*committed_[i],
                                        static_cast<uint16_t>(width));
            auto cell_lines = buffer_to_lines(cell_buf);
            lines.insert(lines.end(), cell_lines.begin(), cell_lines.end());
            lines.push_back("");
        }
        return lines;
    }

    void mark_scrollback_drained() {
        scrollback_watermark_ = committed_.size();
    }

    std::vector<std::string> drain_scrollback(size_t width) {
        auto lines = pending_scrollback(width);
        mark_scrollback_drained();
        return lines;
    }

    Buffer render_active_buffer(uint16_t width) const {
        if (!active_ || width == 0) return {};
        return render_cell(*active_, width);
    }

    uint16_t render_active(Buffer& dst, uint16_t width, uint16_t max_height) const {
        if (!active_ || max_height == 0 || width == 0) return 0;
        auto cell_buf = render_active_buffer(width);
        const auto visible = std::min<uint16_t>(cell_buf.h, max_height);
        const auto first = static_cast<uint16_t>(cell_buf.h - visible);
        for (uint16_t y = 0; y < visible && y < dst.h; ++y) {
            for (uint16_t x = 0; x < width && x < dst.w; ++x) {
                dst.at(x, y) = cell_buf.at(x, first + y);
            }
        }
        return visible;
    }
};

} // namespace merak::tui
