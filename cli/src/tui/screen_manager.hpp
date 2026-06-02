#pragma once
#include "chat_timeline.hpp"
#include "composer/chat_composer.hpp"
#include "components/status_bar.hpp"
#include "inline_terminal.hpp"
#include "terminal_event_reader.hpp"
#include "../commands/command_registry.hpp"
#include <atomic>
#include <chrono>
#include <cctype>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace merak::tui {

class ScreenManager {
    enum class Overlay { None, Help, Context, Model, Tools, Memory, Transcript, ToolBrowser, ToolDetail };

    ChatTimeline timeline_;
    ChatComposer composer_;
    StatusBar status_bar_;
    InlineTerminal terminal_;
    TerminalEventReader reader_;
    std::function<void(std::string)> on_command_;
    std::function<void()> on_cancel_;
    std::queue<std::function<void()>> ui_events_;
    std::mutex ui_events_mutex_;
    std::thread worker_;
    std::queue<std::string> queued_messages_;
    std::function<void(bool)> pending_approval_;
    std::unique_ptr<ApprovalCell> approval_cell_;
    std::atomic<bool> busy_ = false;
    bool exit_requested_ = false;
    bool running_ = true;
    Overlay overlay_ = Overlay::None;
    int overlay_selected_ = 0;
    size_t overlay_scroll_ = 0;
    int last_prompt_tokens_ = 0;
    int total_input_tokens_ = 0;
    int total_output_tokens_ = 0;
    int completed_turns_ = 0;
    int messages_ = 0;
    int tool_calls_ = 0;
    bool has_usage_ = false;
    bool usage_missing_ = false;
    std::string system_prompt_;

    void drain_ui_events() {
        std::queue<std::function<void()>> events;
        {
            std::lock_guard<std::mutex> lock(ui_events_mutex_);
            events.swap(ui_events_);
        }
        while (!events.empty()) {
            events.front()();
            events.pop();
        }
    }

    void resolve_approval(bool allowed) {
        if (!pending_approval_) return;
        auto approval = std::move(pending_approval_);
        approval_cell_.reset();
        status_bar_.set_state("Thinking...");
        approval(allowed);
    }

    void submit(std::string input) {
        if (input.empty()) return;
        if (input.starts_with("/")) {
            while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back()))) {
                input.pop_back();
            }
            if (on_command_) on_command_(std::move(input));
            return;
        }
        timeline_.submit_user(input);
        ++messages_;
        if (busy_) {
            queued_messages_.push(std::move(input));
            return;
        }
        if (on_command_) on_command_(std::move(input));
    }

    void submit_composer() {
        auto input = composer_.submit();
        submit(std::move(input));
    }

    void handle_overlay(const TerminalEvent& event) {
        if (event.type == TerminalEvent::Type::Escape) {
            overlay_ = overlay_ == Overlay::ToolDetail ? Overlay::ToolBrowser : Overlay::None;
            return;
        }
        if (overlay_ == Overlay::Transcript || overlay_ == Overlay::ToolDetail) {
            if (event.type == TerminalEvent::Type::Up) ++overlay_scroll_;
            if (event.type == TerminalEvent::Type::Down && overlay_scroll_ > 0) --overlay_scroll_;
            return;
        }
        if (overlay_ != Overlay::ToolBrowser) return;
        const auto count = static_cast<int>(timeline_.tools().size());
        if (event.type == TerminalEvent::Type::Up && overlay_selected_ > 0) --overlay_selected_;
        if (event.type == TerminalEvent::Type::Down && overlay_selected_ + 1 < count) ++overlay_selected_;
        if (event.type == TerminalEvent::Type::Enter && count > 0) {
            overlay_scroll_ = 0;
            overlay_ = Overlay::ToolDetail;
        }
    }

    void handle_event(const TerminalEvent& event) {
        using Type = TerminalEvent::Type;
        if (event.type == Type::None) return;
        if (pending_approval_) {
            if (event.type == Type::Escape || (event.type == Type::Character && (event.character == 'n' || event.character == 'N'))) {
                resolve_approval(false);
            } else if (event.type == Type::Character && (event.character == 'y' || event.character == 'Y')) {
                resolve_approval(true);
            }
            return;
        }
        if (overlay_ != Overlay::None) {
            handle_overlay(event);
            return;
        }
        if (event.type == Type::CtrlC) {
            if (busy_) {
                status_bar_.set_state("Cancelling...");
                if (on_cancel_) on_cancel_();
            } else if (!composer_.empty()) {
                composer_.clear();
            } else {
                running_ = false;
            }
            return;
        }
        if (event.type == Type::CtrlD && composer_.empty() && !busy_) { running_ = false; return; }
        if (event.type == Type::CtrlO) { open_transcript(); return; }
        if (event.type == Type::CtrlT) { open_tool_browser(); return; }
        if (event.type == Type::F1) { open_help(); return; }
        if (event.type == Type::CtrlL) { terminal_.invalidate(); return; }
        if (event.type == Type::Paste) { composer_.handle_paste(event.text); return; }
        if (event.type == Type::Enter) { submit_composer(); return; }
        if (event.type == Type::ShiftEnter) { composer_.newline(); return; }
        if (event.type == Type::Backspace) { composer_.backspace(); return; }
        if (event.type == Type::DeleteKey) { composer_.delete_forward(); return; }
        if (event.type == Type::Left) { composer_.move_left(); return; }
        if (event.type == Type::Right) { composer_.move_right(); return; }
        if (event.type == Type::Home || event.type == Type::CtrlA) { composer_.move_home(); return; }
        if (event.type == Type::End || event.type == Type::CtrlE) { composer_.move_end(); return; }
        if (event.type == Type::CtrlK) { composer_.kill_to_end(); return; }
        if (event.type == Type::CtrlU) { composer_.kill_to_start(); return; }
        if (event.type == Type::CtrlW) { composer_.delete_word_left(); return; }
        if (event.type == Type::CtrlY) { composer_.yank(); return; }
        if (event.type == Type::Tab) { composer_.slash_complete(); return; }
        if (event.type == Type::Up) {
            if (composer_.slash_open()) composer_.slash_prev(); else composer_.history_prev();
            return;
        }
        if (event.type == Type::Down) {
            if (composer_.slash_open()) composer_.slash_next(); else composer_.history_next();
            return;
        }
        if (event.type == Type::Character) composer_.insert_char(event.character);
    }

    std::vector<std::string> overlay_lines() const {
        std::vector<std::string> lines;
        if (overlay_ == Overlay::Help) {
            lines = {ansi(theme::ANSI_ACCENT, "Help"), ""};
            for (const auto& cmd : commands::all_commands()) {
                lines.push_back("  " + cmd.name + "  " + ansi(theme::ANSI_DIM, cmd.description));
            }
            lines.push_back("");
            lines.push_back(ansi(theme::ANSI_DIM, "Esc back · Ctrl+O transcript · Ctrl+T tools · Ctrl+D exit"));
        } else if (overlay_ == Overlay::Context) {
            lines = {ansi(theme::ANSI_ACCENT, "Context"), "",
                "  Last prompt      " + std::to_string(last_prompt_tokens_),
                "  Cumulative input " + std::to_string(total_input_tokens_),
                "  Output tokens    " + std::to_string(total_output_tokens_),
                "  Messages         " + std::to_string(messages_),
                "  Completed turns  " + std::to_string(completed_turns_),
                "  Tool calls       " + std::to_string(tool_calls_),
                "", ansi(theme::ANSI_DIM, "Esc back")};
        } else if (overlay_ == Overlay::Transcript) {
            lines = {ansi(theme::ANSI_ACCENT, "Transcript"), ""};
            std::vector<std::string> content;
            const auto& cells = timeline_.committed();
            for (size_t i = 0; i < cells.size(); ++i) {
                auto rendered = cells[i]->render(terminal_.width());
                content.insert(content.end(), rendered.begin(), rendered.end());
                content.push_back("");
            }
            append_scrolled(lines, content);
            lines.push_back(ansi(theme::ANSI_DIM, "Esc back · persisted by merak serve"));
        } else if (overlay_ == Overlay::ToolBrowser) {
            lines = {ansi(theme::ANSI_ACCENT, "Tool Browser"), ""};
            const auto& tools = timeline_.tools();
            if (tools.empty()) lines.push_back(ansi(theme::ANSI_DIM, "  No tool calls yet"));
            for (size_t i = 0; i < tools.size(); ++i) {
                lines.push_back(std::string(i == static_cast<size_t>(overlay_selected_) ? "› " : "  ")
                    + sanitize_terminal_text(tools[i]->call().name) + "  "
                    + ansi(theme::ANSI_DIM, tools[i]->description()));
            }
            lines.push_back("");
            lines.push_back(ansi(theme::ANSI_DIM, "↑/↓ select · Enter detail · Esc back"));
        } else if (overlay_ == Overlay::ToolDetail) {
            const auto& tools = timeline_.tools();
            if (tools.empty()) return {ansi(theme::ANSI_DIM, "No tool calls yet")};
            auto selected = tools[std::min<size_t>(overlay_selected_, tools.size() - 1)];
            lines = {ansi(theme::ANSI_ACCENT, sanitize_terminal_text(selected->call().name)), "",
                ansi(theme::ANSI_DIM, "Arguments")};
            std::vector<std::string> content;
            auto arguments = split_lines(sanitize_terminal_text(selected->call().arguments));
            content.insert(content.end(), arguments.begin(), arguments.end());
            content.push_back("");
            content.push_back(ansi(theme::ANSI_DIM, "Output"));
            auto output = split_lines(selected->output());
            content.insert(content.end(), output.begin(), output.end());
            append_scrolled(lines, content);
            lines.push_back("");
            lines.push_back(ansi(theme::ANSI_DIM, "↑/↓ scroll · Esc back"));
        } else if (overlay_ == Overlay::Model) {
            lines = {ansi(theme::ANSI_ACCENT, "Model"), "", "  Model selection remains configured through settings.", "", ansi(theme::ANSI_DIM, "Esc back")};
        } else if (overlay_ == Overlay::Memory) {
            lines = {ansi(theme::ANSI_ACCENT, "Memory"), "", "  Memory browser is not populated in this runtime.", "", ansi(theme::ANSI_DIM, "Esc back")};
        } else if (overlay_ == Overlay::Tools) {
            lines = {ansi(theme::ANSI_ACCENT, "Tools"), "", "  Press Ctrl+T to browse tool calls from this session.", "", ansi(theme::ANSI_DIM, "Esc back")};
        }
        return lines;
    }

    void append_scrolled(std::vector<std::string>& lines,
                         const std::vector<std::string>& content) const {
        const auto capacity = terminal_.height() > 7 ? terminal_.height() - 7 : 1;
        if (content.size() <= capacity) {
            lines.insert(lines.end(), content.begin(), content.end());
            return;
        }
        const auto max_scroll = content.size() - capacity;
        const auto scroll = std::min(overlay_scroll_, max_scroll);
        const auto start = content.size() - capacity - scroll;
        lines.insert(lines.end(), content.begin() + static_cast<long>(start),
                     content.begin() + static_cast<long>(start + capacity));
    }

    std::vector<std::string> frame_lines() const {
        std::vector<std::string> lines;
        if (timeline_.active()) {
            lines = timeline_.active()->render(terminal_.width());
            lines.push_back("");
        }
        if (approval_cell_) {
            auto approval = approval_cell_->render();
            lines.insert(lines.end(), approval.begin(), approval.end());
            lines.push_back("");
        }
        lines.push_back(ansi(theme::ANSI_DIM, repeat_text("━", terminal_.width())));
        auto pane = overlay_ == Overlay::None ? composer_.render() : overlay_lines();
        lines.insert(lines.end(), pane.begin(), pane.end());
        lines.push_back(ansi(theme::ANSI_DIM, "/ commands · Shift+Enter newline · Ctrl+O transcript · Ctrl+T tools"));
        lines.push_back(ansi(theme::ANSI_DIM,
            sanitize_terminal_text(status_bar_.plain_text(queued_messages_.size()))));
        const auto max_height = terminal_.height();
        if (lines.size() > max_height) {
            lines.erase(lines.begin(), lines.end() - static_cast<long>(max_height));
        }
        return lines;
    }

public:
    ScreenManager() = default;
    ~ScreenManager() { if (worker_.joinable()) worker_.join(); }
    StatusBar& status_bar() { return status_bar_; }
    ChatTimeline& timeline() { return timeline_; }
    void set_on_command(std::function<void(std::string)> fn) { on_command_ = std::move(fn); }
    void set_on_cancel(std::function<void()> fn) { on_cancel_ = std::move(fn); }
    void set_system_prompt(std::string prompt) { system_prompt_ = std::move(prompt); }

    void post(std::function<void()> fn) {
        std::lock_guard<std::mutex> lock(ui_events_mutex_);
        ui_events_.push(std::move(fn));
    }
    bool start_background(std::function<void()> fn) {
        if (busy_) return false;
        if (worker_.joinable()) worker_.join();
        busy_ = true;
        worker_ = std::thread([fn = std::move(fn)] { fn(); });
        return true;
    }
    void finish_background() {
        busy_ = false;
        if (exit_requested_) {
            running_ = false;
            return;
        }
        if (!queued_messages_.empty()) {
            auto next = std::move(queued_messages_.front());
            queued_messages_.pop();
            if (on_command_) on_command_(std::move(next));
        }
    }
    void finish_remote_run() { finish_background(); }
    bool busy() const { return busy_; }
    size_t queued_messages() const { return queued_messages_.size(); }
    void reset_timeline() { timeline_.clear(); }
    void exit() {
        if (busy_) {
            exit_requested_ = true;
            status_bar_.set_state("Finishing before exit...");
        } else {
            running_ = false;
        }
    }
    void request_approval(const ToolCall& call, std::function<void(bool)> approval) {
        post([this, call, approval = std::move(approval)] {
            pending_approval_ = approval;
            auto detail = call.arguments.empty() ? call.name
                : call.name + " " + truncate_text(sanitize_terminal_text(call.arguments), 72);
            approval_cell_ = std::make_unique<ApprovalCell>("Allow " + detail + "?");
            status_bar_.set_state("Waiting for approval...");
        });
    }
    void record_usage(int input, int output, bool has_usage) {
        status_bar_.add_usage(input, output, has_usage);
        if (!has_usage) { usage_missing_ = true; return; }
        last_prompt_tokens_ = input;
        total_input_tokens_ += input;
        total_output_tokens_ += output;
        has_usage_ = true;
    }
    void record_turn_complete() { ++completed_turns_; ++messages_; }
    void record_tool_start() { ++tool_calls_; }
    void record_tool_end() { ++messages_; }
    void open_help() { overlay_ = Overlay::Help; }
    void open_context() { overlay_ = Overlay::Context; }
    void open_model_selector() { overlay_ = Overlay::Model; }
    void open_tools() { overlay_ = Overlay::Tools; }
    void open_memory() { overlay_ = Overlay::Memory; }
    void open_transcript() { overlay_scroll_ = 0; overlay_ = Overlay::Transcript; }
    void open_tool_browser() {
        overlay_selected_ = timeline_.tools().empty()
            ? 0 : static_cast<int>(timeline_.tools().size() - 1);
        overlay_scroll_ = 0;
        overlay_ = Overlay::ToolBrowser;
    }

    void run() {
        while (running_) {
            drain_ui_events();
            terminal_.flush_scrollback(timeline_.drain_scrollback(terminal_.width()));
            terminal_.redraw(frame_lines());
            handle_event(reader_.next());
        }
    }
};

} // namespace merak::tui
