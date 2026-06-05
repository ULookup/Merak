#pragma once
#include "chat_timeline.hpp"
#include "composer/chat_composer.hpp"
#include "components/status_bar.hpp"
#include "buffer.hpp"
#include "diff_terminal.hpp"
#include "terminal_event_reader.hpp"
#include "../commands/command_registry.hpp"
#include "overlay/resume_view.hpp"
#include "persistence/resume.hpp"
#include "persistence/transcript.hpp"
#include <nlohmann/json.hpp>
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
    enum class Overlay { None, Help, Context, Model, Tools, Memory, Transcript, ToolBrowser, ToolDetail, Resume };

    ChatTimeline timeline_;
    ChatComposer composer_;
    StatusBar status_bar_;
    DiffTerminal terminal_;
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
    bool memory_detail_ = false;
    std::string memory_filter_;
    ResumeView resume_view_;
    std::string session_id_;
    size_t persisted_cell_count_ = 0;
    mutable uint16_t composer_start_y_ = 0;
    int last_prompt_tokens_ = 0;
    int total_input_tokens_ = 0;
    int total_output_tokens_ = 0;
    int completed_turns_ = 0;
    int messages_ = 0;
    int tool_calls_ = 0;
    int running_agents_ = 0;
    bool has_usage_ = false;
    bool usage_missing_ = false;
    std::string system_prompt_;
    nlohmann::json runtime_metadata_ = nlohmann::json::object();
    nlohmann::json memory_items_json_ = nlohmann::json::array();
    std::string selected_model_;
    std::chrono::steady_clock::time_point submit_flash_until_{};

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
        status_bar_.set_pending_approvals(0);
        status_bar_.set_state("Thinking...");
        approval(allowed);
    }

    void submit(std::string input) {
        if (input.empty()) return;
        if (input.starts_with("/")) {
            while (!input.empty() && std::isspace(static_cast<unsigned char>(input.back()))) {
                input.pop_back();
            }
            if (input == "/resume") {
                open_resume();
                return;
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
        composer_.set_submit_flash(true);
        submit_flash_until_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(180);
        submit(std::move(input));
    }

    void handle_overlay(const TerminalEvent& event) {
        if (event.type == TerminalEvent::Type::Escape) {
            overlay_ = overlay_ == Overlay::ToolDetail ? Overlay::ToolBrowser : Overlay::None;
            return;
        }
        if (overlay_ == Overlay::Resume) {
            if (event.type == TerminalEvent::Type::Up) resume_view_.handle_key("up");
            if (event.type == TerminalEvent::Type::Down) resume_view_.handle_key("down");
            if (event.type == TerminalEvent::Type::Enter) {
                auto sid = resume_view_.selected_sid();
                if (!sid.empty()) {
                    auto result = persistence::restore(sid);
                    timeline_ = std::move(result.timeline);
                    session_id_ = sid;
                    persisted_cell_count_ = timeline_.committed().size();
                    status_bar_.set_state("Restored " + std::to_string(result.event_count) + " events");
                }
                overlay_ = Overlay::None;
            }
            if (event.type == TerminalEvent::Type::CtrlD) {
                auto sid = resume_view_.selected_sid();
                if (!sid.empty()) {
                    persistence::delete_session(sid);
                    resume_view_ = ResumeView{};
                }
            }
            if (event.type == TerminalEvent::Type::Backspace) {
                auto f = resume_view_.get_filter();
                if (!f.empty()) { f.pop_back(); resume_view_.set_filter(f); }
            }
            if (event.type == TerminalEvent::Type::Character && event.text != "/") {
                resume_view_.set_filter(resume_view_.get_filter() + event.text);
            }
            return;
        }
        if (overlay_ == Overlay::Memory) {
            auto items = memory_items();
            if (event.type == TerminalEvent::Type::Up && overlay_selected_ > 0) --overlay_selected_;
            if (event.type == TerminalEvent::Type::Down && overlay_selected_ + 1 < static_cast<int>(items.size())) ++overlay_selected_;
            if (event.type == TerminalEvent::Type::Enter && !items.empty()) { memory_detail_ = !memory_detail_; overlay_scroll_ = 0; }
            if (event.type == TerminalEvent::Type::Backspace && !memory_filter_.empty()) { memory_filter_.pop_back(); overlay_selected_ = 0; }
            if (event.type == TerminalEvent::Type::Character) { memory_filter_ += event.text; overlay_selected_ = 0; memory_detail_ = false; }
            return;
        }
        if (overlay_ == Overlay::Transcript || overlay_ == Overlay::ToolDetail) {
            if (event.type == TerminalEvent::Type::Up) ++overlay_scroll_;
            if (event.type == TerminalEvent::Type::Down && overlay_scroll_ > 0) --overlay_scroll_;
            return;
        }
        if (overlay_ == Overlay::Model) {
            const auto count = static_cast<int>(runtime_metadata_.value("models", nlohmann::json::array()).size());
            if (event.type == TerminalEvent::Type::Up && overlay_selected_ > 0) --overlay_selected_;
            if (event.type == TerminalEvent::Type::Down && overlay_selected_ + 1 < count) ++overlay_selected_;
            if (event.type == TerminalEvent::Type::Enter && count > 0) {
                const auto& model = runtime_metadata_["models"][overlay_selected_];
                selected_model_ = model.value("name", runtime_metadata_.value("model", ""));
                status_bar_.set_model(selected_model_);
                overlay_ = Overlay::None;
            }
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
            if (event.type == Type::Escape || (event.type == Type::Character && (event.text == "n" || event.text == "N"))) {
                resolve_approval(false);
            } else if (event.type == Type::Character && (event.text == "y" || event.text == "Y")) {
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
        if (event.type == Type::CtrlE) {
            terminal_.with_cooked_terminal([this] { composer_.open_external_editor(); });
            return;
        }
        if (event.type == Type::Paste) { composer_.handle_paste(event.text); return; }
        if (event.type == Type::Enter) { if (composer_.mention_open()) composer_.mention_accept(); else submit_composer(); return; }
        if (event.type == Type::ShiftEnter) { composer_.newline(); return; }
        if (event.type == Type::Backspace) { composer_.backspace(); return; }
        if (event.type == Type::DeleteKey) { composer_.delete_forward(); return; }
        if (event.type == Type::Left) { composer_.move_left(); return; }
        if (event.type == Type::Right) { composer_.move_right(); return; }
        if (event.type == Type::Home || event.type == Type::CtrlA) { composer_.move_home(); return; }
        if (event.type == Type::End) { composer_.move_end(); return; }
        if (event.type == Type::CtrlK) { composer_.kill_to_end(); return; }
        if (event.type == Type::CtrlU) { composer_.kill_to_start(); return; }
        if (event.type == Type::CtrlW) { composer_.delete_word_left(); return; }
        if (event.type == Type::CtrlY) { composer_.yank(); return; }
        if (event.type == Type::Tab) { composer_.slash_complete(); return; }
        if (event.type == Type::Up) {
            if (composer_.mention_open()) composer_.mention_prev();
            else if (composer_.slash_open()) composer_.slash_prev(); else composer_.history_prev();
            return;
        }
        if (event.type == Type::Down) {
            if (composer_.mention_open()) composer_.mention_next();
            else if (composer_.slash_open()) composer_.slash_next(); else composer_.history_next();
            return;
        }
        if (event.type == Type::Character) composer_.insert_text(event.text);
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
            static constexpr uint16_t kMaxCellHeight = 80;
            for (size_t i = 0; i < cells.size(); ++i) {
                Buffer cell_buf;
                cell_buf.resize(terminal_.width(), kMaxCellHeight);
                cells[i]->render(cell_buf, terminal_.width());
                auto rendered = buffer_to_lines(cell_buf);
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
            lines = {ansi(theme::ANSI_ACCENT, "Model"), ""};
            auto models = runtime_metadata_.value("models", nlohmann::json::array());
            if (models.empty()) {
                lines.push_back(ansi(theme::ANSI_DIM, "  No configured models"));
            }
            for (size_t i = 0; i < models.size(); ++i) {
                auto name = models[i].value("name", "");
                auto provider = models[i].value("provider", "");
                const bool current = name == selected_model_ || (selected_model_.empty() && name == runtime_metadata_.value("model", ""));
                lines.push_back(std::string(i == static_cast<size_t>(overlay_selected_) ? "› " : "  ")
                    + (current ? ansi(theme::ANSI_ACCENT, name) : name)
                    + ansi(theme::ANSI_DIM, "  " + provider));
            }
            lines.push_back("");
            lines.push_back(ansi(theme::ANSI_DIM, "↑/↓ select · Enter switch · Esc back"));
        } else if (overlay_ == Overlay::Memory) {
            lines = {ansi(theme::ANSI_ACCENT, "Memory"), ""};
            auto memory = runtime_metadata_.value("memory", nlohmann::json::object());
            lines.push_back("  status  " + std::string(memory.value("enabled", false) ? "enabled" : "disabled")
                + "  filter " + (memory_filter_.empty() ? ansi(theme::ANSI_DIM, "<type to search>") : memory_filter_));
            lines.push_back("");
            auto items = memory_items();
            if (items.empty()) {
                lines.push_back(ansi(theme::ANSI_DIM, "  No matching memories"));
            } else if (memory_detail_) {
                auto item = items[std::min<size_t>(overlay_selected_, items.size() - 1)];
                lines.push_back(ansi(theme::ANSI_ACCENT, item.title));
                lines.push_back("");
                auto body = split_lines(item.body);
                append_scrolled(lines, body);
            } else {
                for (size_t i = 0; i < items.size(); ++i) {
                    lines.push_back(std::string(i == static_cast<size_t>(overlay_selected_) ? "› " : "  ")
                        + items[i].title + ansi(theme::ANSI_DIM, "  " + truncate_text(items[i].body, 80)));
                }
            }
            lines.push_back("");
            lines.push_back(ansi(theme::ANSI_DIM, "type search · ↑/↓ select · Enter detail · Esc back"));
        } else if (overlay_ == Overlay::Tools) {
            lines = {ansi(theme::ANSI_ACCENT, "Tools"), ""};
            for (const auto& tool : runtime_metadata_.value("tools", nlohmann::json::array())) {
                auto name = tool.value("name", "");
                auto source = tool.value("source", "");
                lines.push_back("  " + name + ansi(theme::ANSI_DIM, "  " + source)
                    + "  calls " + std::to_string(tool_call_count(name)));
            }
            for (const auto& server : runtime_metadata_.value("mcp_servers", nlohmann::json::array())) {
                lines.push_back("  MCP " + server.value("name", "") + "  "
                    + std::string(server.value("alive", false) ? "connected" : "offline"));
            }
            lines.push_back("");
            lines.push_back("  calls this session " + std::to_string(tool_calls_));
            lines.push_back(ansi(theme::ANSI_DIM, "Esc back · Ctrl+T call history"));
        }
        return lines;
    }

    struct MemoryItem {
        std::string title;
        std::string body;
    };

    std::vector<MemoryItem> memory_items() const {
        std::vector<MemoryItem> items;
        const auto filter = lower_ascii(memory_filter_);
        int index = 0;
        for (const auto& item : memory_items_json_) {
            auto title = std::to_string(item.value("index", 0)) + " " + item.value("role", "memory");
            auto body = item.value("content", "");
            auto haystack = lower_ascii(title + "\n" + body);
            if (!filter.empty() && haystack.find(filter) == std::string::npos) continue;
            items.push_back({title, sanitize_terminal_text(body)});
        }
        if (!items.empty()) return items;
        for (const auto& cell : timeline_.committed()) {
            auto json = cell->to_json();
            auto type = json.value("type", "item");
            std::string body;
            if (json.contains("text")) body = json.value("text", "");
            else if (json.contains("markdown")) body = json.value("markdown", "");
            else if (json.contains("output")) body = json.value("output", "");
            else body = json.dump();
            auto title = std::to_string(++index) + " " + type;
            auto haystack = lower_ascii(title + "\n" + body);
            if (!filter.empty() && haystack.find(filter) == std::string::npos) continue;
            items.push_back({title, sanitize_terminal_text(body)});
        }
        return items;
    }

    int tool_call_count(const std::string& name) const {
        int count = 0;
        for (const auto& tool : timeline_.tools()) {
            if (tool->call().name == name) ++count;
        }
        return count;
    }

    static std::string lower_ascii(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
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

    static void copy_to(Buffer& dst, const Buffer& src, uint16_t dst_y) {
        for (uint16_t sy = 0; sy < src.h && dst_y + sy < dst.h; ++sy) {
            for (uint16_t sx = 0; sx < src.w && sx < dst.w; ++sx) {
                dst.at(sx, dst_y + sy) = src.at(sx, sy);
            }
        }
    }

    void frame_buffer(Buffer& buf) const {
        buf.clear();
        auto& t = theme::active_theme();
        Style dim_style; dim_style.fg = t.dim; dim_style.dim(true);
        uint16_t w = buf.w;
        uint16_t y = 0;

        if (overlay_ == Overlay::Resume) {
            resume_view_.render(buf, w);
            return;
        }

        static constexpr uint16_t kActiveCellMaxH = 80;
        if (timeline_.active()) {
            Buffer cell_buf;
            cell_buf.resize(w, kActiveCellMaxH);
            timeline_.active()->render(cell_buf, w);
            copy_to(buf, cell_buf, y);
            y += cell_buf.h + 1;
        }
        if (approval_cell_) {
            Buffer approval_buf;
            approval_buf.resize(w, 2);
            approval_cell_->render(approval_buf);
            copy_to(buf, approval_buf, y);
            y += approval_buf.h + 1;
        }

        buf.set_span(0, y, repeat_text("━", w), dim_style);
        ++y;

        if (overlay_ == Overlay::None) {
            composer_start_y_ = y;
            Buffer composer_buf;
            composer_buf.resize(w, 15);
            composer_.render(composer_buf);
            copy_to(buf, composer_buf, y);
            y += composer_buf.h;
        } else {
            auto pane_lines = overlay_lines();
            for (size_t i = 0; i < pane_lines.size() && y < buf.h; ++i) {
                buf.set_span(0, y, sanitize_terminal_text(pane_lines[i]), dim_style);
                ++y;
            }
        }

        buf.set_span(0, y, "/ commands · Shift+Enter newline · Ctrl+O transcript · Ctrl+T tools", dim_style);
        ++y;
        status_bar_.render(buf, w, y, queued_messages_.size());
    }

public:
    ScreenManager() = default;
    ~ScreenManager() { if (worker_.joinable()) worker_.join(); }
    StatusBar& status_bar() { return status_bar_; }
    ChatTimeline& timeline() { return timeline_; }
    const std::string& selected_model() const { return selected_model_; }
    void set_on_command(std::function<void(std::string)> fn) { on_command_ = std::move(fn); }
    void set_on_cancel(std::function<void()> fn) { on_cancel_ = std::move(fn); }
    void set_system_prompt(std::string prompt) { system_prompt_ = std::move(prompt); }
    void set_runtime_metadata(nlohmann::json metadata) {
        runtime_metadata_ = std::move(metadata);
        selected_model_ = runtime_metadata_.value("model", "");
        status_bar_.set_permission_mode(runtime_metadata_.value("permission_mode", "Prompt"));
        int budget = 128000;
        for (const auto& model : runtime_metadata_.value("models", nlohmann::json::array())) {
            if (model.value("name", "") == selected_model_) budget = model.value("max_context_tokens", budget);
        }
        status_bar_.set_token_budget(budget, total_input_tokens_ + total_output_tokens_);
    }
    void set_memory_items(nlohmann::json items) { memory_items_json_ = std::move(items); }

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
            status_bar_.set_pending_approvals(1);
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
        int budget = 128000;
        for (const auto& model : runtime_metadata_.value("models", nlohmann::json::array())) {
            if (model.value("name", "") == selected_model_) budget = model.value("max_context_tokens", budget);
        }
        status_bar_.set_token_budget(budget, total_input_tokens_ + total_output_tokens_);
        status_bar_.set_estimated_cost((total_input_tokens_ / 1000000.0) * 2.50
            + (total_output_tokens_ / 1000000.0) * 10.00);
    }
    void record_turn_complete() { ++completed_turns_; ++messages_; persist_turn(); }
    void record_tool_start() { ++tool_calls_; }
    void record_tool_end() { ++messages_; }
    void record_agent_start() { ++running_agents_; status_bar_.set_running_agents(running_agents_); }
    void record_agent_end() { if (running_agents_ > 0) --running_agents_; status_bar_.set_running_agents(running_agents_); }
    void open_help() { overlay_ = Overlay::Help; }
    void open_context() { overlay_ = Overlay::Context; }
    void open_model_selector() { overlay_ = Overlay::Model; }
    void open_tools() { overlay_ = Overlay::Tools; }
    void open_memory() { overlay_selected_ = 0; overlay_scroll_ = 0; memory_detail_ = false; overlay_ = Overlay::Memory; }
    void open_transcript() { overlay_scroll_ = 0; overlay_ = Overlay::Transcript; }
    void open_tool_browser() {
        overlay_selected_ = timeline_.tools().empty()
            ? 0 : static_cast<int>(timeline_.tools().size() - 1);
        overlay_scroll_ = 0;
        overlay_ = Overlay::ToolBrowser;
    }
    void open_resume() {
        resume_view_ = ResumeView{};
        overlay_ = Overlay::Resume;
    }

    void ensure_session_id() {
        if (!session_id_.empty()) return;
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        session_id_ = "session_" + std::to_string(now);
        persistence::SessionMeta meta;
        meta.session_id = session_id_;
        meta.created_at = static_cast<uint64_t>(now);
        meta.model = selected_model_;
        meta.provider = status_bar_.provider();
        meta.cwd = status_bar_.cwd();
        persistence::update_index(session_id_, meta);
    }

    void persist_turn() {
        ensure_session_id();
        const auto& cells = timeline_.committed();
        for (size_t i = persisted_cell_count_; i < cells.size(); ++i) {
            auto json = cells[i]->to_json();
            auto type = json.value("type", "");
            if (type == "welcome") continue;
            if (type == "user") {
                persistence::append_event(session_id_,
                    persistence::UserEvent{json.value("text", ""),
                        static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count())});
            } else if (type == "assistant") {
                persistence::append_event(session_id_,
                    persistence::AssistantEvent{json.value("markdown", ""), selected_model_,
                        static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count())});
            } else if (type == "tool") {
                persistence::ToolEvent e;
                e.tool_name = json.value("name", "");
                e.output = json.value("output", "");
                e.exit_code = json.value("exit_code", 0);
                auto status_str = json.value("status", "success");
                if (status_str == "running") e.status = persistence::ToolEvent::Status::running;
                else if (status_str == "failed") e.status = persistence::ToolEvent::Status::error;
                else e.status = persistence::ToolEvent::Status::success;
                persistence::append_event(session_id_, e);
            } else if (type == "system") {
                auto level = json.value("error", false)
                    ? persistence::SystemEvent::Level::error : persistence::SystemEvent::Level::info;
                persistence::append_event(session_id_,
                    persistence::SystemEvent{json.value("text", ""),
                        level,
                        static_cast<uint64_t>(
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count())});
            } else if (type == "turn_summary") {
                persistence::append_event(session_id_,
                    persistence::TurnSummaryEvent{
                        static_cast<uint32_t>(json.value("tokens_in", 0)),
                        static_cast<uint32_t>(json.value("tokens_out", 0)),
                        0.0,
                        static_cast<uint64_t>(json.value("elapsed_ms", 0)),
                        static_cast<uint32_t>(json.value("tools", 0))});
            }
        }
        persisted_cell_count_ = cells.size();
    }

    void run() {
        while (running_) {
            drain_ui_events();
            if (submit_flash_until_ != std::chrono::steady_clock::time_point{}
                && std::chrono::steady_clock::now() > submit_flash_until_) {
                composer_.set_submit_flash(false);
                submit_flash_until_ = {};
            }
            terminal_.flush_scrollback(timeline_.drain_scrollback(terminal_.width()));
            Buffer frame_buf;
            frame_buf.resize(terminal_.width(), terminal_.height());
            frame_buffer(frame_buf);
            terminal_.draw(frame_buf);
            if (overlay_ == Overlay::None) {
                size_t col = 2 + composer_.cursor_col_in_line();
                int cursor_y = static_cast<int>(composer_start_y_) + static_cast<int>(composer_.cursor_line());
                int up = static_cast<int>(terminal_.height()) - cursor_y - 1;
                if (up > 0) std::cout << "\x1b[" << up << "A\r\x1b[" << col << "C" << std::flush;
            }
            handle_event(reader_.next());
        }
    }
};

} // namespace merak::tui
