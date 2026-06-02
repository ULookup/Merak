#pragma once
#include "history_cell/history_cell.hpp"
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace merak::tui {

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
        while (scrollback_watermark_ < committed_.size()) {
            auto rendered = committed_[scrollback_watermark_++]->render(width);
            lines.insert(lines.end(), rendered.begin(), rendered.end());
            lines.push_back("");
        }
        return lines;
    }
};

} // namespace merak::tui
