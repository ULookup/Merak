#pragma once
#include "history_cell/history_cell.hpp"
#include "buffer.hpp"
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace merak::tui {

inline std::vector<std::string> buffer_to_lines(const Buffer& buf) {
    std::vector<std::string> lines;
    for (uint16_t y = 0; y < buf.h; ++y) {
        std::string line;
        Style curr;
        for (uint16_t x = 0; x < buf.w; ++x) {
            const auto& cell = buf.at(x, y);
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

    std::vector<std::string> drain_scrollback(size_t width) {
        std::vector<std::string> lines;
        static constexpr uint16_t kMaxCellHeight = 80;
        while (scrollback_watermark_ < committed_.size()) {
            Buffer cell_buf;
            cell_buf.resize(width, kMaxCellHeight);
            committed_[scrollback_watermark_++]->render(cell_buf, width);
            auto cell_lines = buffer_to_lines(cell_buf);
            lines.insert(lines.end(), cell_lines.begin(), cell_lines.end());
            lines.push_back("");
        }
        return lines;
    }
};

} // namespace merak::tui
