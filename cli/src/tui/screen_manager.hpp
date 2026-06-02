#pragma once
#include "chat_model.hpp"
#include "events.hpp"
#include "inline_terminal.hpp"
#include "panel.hpp"
#include "components/status_bar.hpp"
#include "panels/command_palette.hpp"
#include "panels/context_panel.hpp"
#include "panels/help_panel.hpp"
#include "panels/memory_panel.hpp"
#include "panels/model_selector.hpp"
#include "panels/tools_panel.hpp"
#include "../commands/command_registry.hpp"
#include <merak/sub_agent_runner.hpp>
#include <chrono>
#include <atomic>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <vector>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

namespace merak::tui {

class ScreenManager {
    ChatModel model_;
    std::vector<std::unique_ptr<Panel>> overlay_stack_;
    StatusBar status_bar_;
    ftxui::ScreenInteractive screen_ = ftxui::ScreenInteractive::TerminalOutput();
    std::queue<TuiEvent> events_;
    std::mutex events_mutex_;
    std::thread worker_;
    std::thread ticker_;
    std::atomic<bool> stop_ticker_{false};
    std::atomic<bool> busy_{false};
    bool exit_requested_ = false;
    std::function<void(std::string)> on_command_;
    std::function<void()> on_interrupt_;
    std::optional<std::string> pending_command_;
    std::shared_ptr<std::promise<bool>> pending_approval_;
    std::map<std::string, std::chrono::steady_clock::time_point> tool_started_at_;
    int last_prompt_tokens_ = 0;
    int total_input_tokens_ = 0;
    int total_output_tokens_ = 0;
    int completed_turns_ = 0;
    int messages_ = 0;
    int tool_calls_ = 0;
    bool has_usage_ = false;
    bool usage_missing_ = false;
    std::string system_prompt_;

    void flush_committed() { flush_scrollback(screen_, model_.drain_committed()); }

    void drain_events() {
        std::queue<TuiEvent> events;
        {
            std::lock_guard<std::mutex> lock(events_mutex_);
            events.swap(events_);
        }
        while (!events.empty()) {
            std::visit([this](auto&& event) { apply(event); }, events.front());
            events.pop();
        }
        flush_committed();
    }

    void apply(const TextDelta& event) { model_.append_answer(event.text); }
    void apply(const ToolStarted& event) {
        tool_calls_++;
        tool_started_at_[event.call.id] = std::chrono::steady_clock::now();
        model_.start_tool(event.call);
    }
    void apply(const ToolEnded& event) {
        messages_++;
        auto started = tool_started_at_.find(event.result.call_id);
        auto duration = started == tool_started_at_.end()
            ? std::chrono::milliseconds(0)
            : std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started->second);
        model_.finish_tool(event.result, duration);
        tool_started_at_.erase(event.result.call_id);
    }
    void apply(const StateChanged& event) {
        switch (event.state) {
            case TurnState::ContextReady: model_.status.set_activity("Preparing context"); break;
            case TurnState::Thinking: model_.status.set_activity(""); break;
            case TurnState::Acting: model_.status.set_activity("Running tools"); break;
            case TurnState::Observing: model_.status.set_activity("Observing"); break;
            case TurnState::Responding: model_.status.set_activity("Responding"); break;
            case TurnState::Complete:
            case TurnState::Error:
            case TurnState::Idle: model_.status.set_activity(""); break;
        }
    }
    void apply(const Usage& event) {
        status_bar_.add_usage(event.input_tokens, event.output_tokens, event.exact);
        if (!event.exact) {
            usage_missing_ = true;
            return;
        }
        last_prompt_tokens_ = event.input_tokens;
        status_bar_.set_context_usage(last_prompt_tokens_);
        total_input_tokens_ += event.input_tokens;
        total_output_tokens_ += event.output_tokens;
        has_usage_ = true;
    }
    void apply(const ApprovalRequested& event) {
        pending_approval_ = event.response;
        model_.request_approval(event.call);
    }
    void apply(const TurnCompleted& event) {
        model_.finish_answer(event.response.text);
        model_.commit(std::make_unique<TurnSummaryCell>(
            event.response.total_input_tokens,
            event.response.total_output_tokens,
            static_cast<int>(event.response.tool_results.size())));
        model_.status.finish_turn();
        completed_turns_++;
        messages_ += 2;
        busy_ = false;
        model_.composer.refill_next_queued();
        if (exit_requested_) screen_.Exit();
    }
    void apply(const TurnFailed& event) {
        model_.finish_answer();
        model_.commit(std::make_unique<SystemCell>(event.message));
        model_.status.finish_turn();
        busy_ = false;
        model_.composer.refill_next_queued();
        if (exit_requested_) screen_.Exit();
    }
    void apply(const Interrupted&) {
        model_.commit(std::make_unique<SystemCell>("Interrupt requested"));
    }
    void apply(const SubAgentStarted& event) { model_.on_agent_started(event.id); }
    void apply(const SubAgentStateChanged& event) { model_.on_agent_state(event.id, event.state); }
    void apply(const SubAgentStep& event) { model_.on_agent_step(event.id); }
    void apply(const SubAgentEnded& event) { model_.on_agent_ended(event.id, event.failed); }

    void resolve_approval(bool allowed) {
        if (!pending_approval_) return;
        auto approval = std::move(pending_approval_);
        model_.clear_approval();
        approval->set_value(allowed);
    }

    ftxui::Element render_viewport() {
        using namespace ftxui;
        if (!overlay_stack_.empty()) {
            return vbox({
                overlay_stack_.back()->render(),
                status_bar_.render(model_.composer.queued_count()),
            });
        }
        Elements rows;
        for (const auto& row : model_.agent_rows()) rows.push_back(text(row) | dim);
        if (auto* approval = model_.approval()) {
            for (const auto& line : approval->lines()) rows.push_back(text(line) | bold);
        } else {
            if (auto* assistant = model_.active_assistant()) {
                for (const auto& line : assistant->lines()) rows.push_back(text(line));
            }
            if (model_.status.active()) {
                rows.push_back(text(model_.status.line()) | color(Color::Palette256(178)));
            }
        }
        rows.push_back(separator());
        rows.push_back(text("> " + model_.composer.text() + " ") | inverted);
        rows.push_back(status_bar_.render(model_.composer.queued_count()));
        return vbox(std::move(rows));
    }

public:
    ScreenManager() = default;
    ~ScreenManager() {
        stop_ticker_ = true;
        if (ticker_.joinable()) ticker_.join();
        if (worker_.joinable()) worker_.join();
    }

    ChatModel& model() { return model_; }
    StatusBar& status_bar() { return status_bar_; }
    void set_on_command(std::function<void(std::string)> fn) { on_command_ = std::move(fn); }
    void set_on_interrupt(std::function<void()> fn) { on_interrupt_ = std::move(fn); }
    void set_system_prompt(std::string prompt) { system_prompt_ = std::move(prompt); }
    bool busy() const { return busy_.load(); }

    void post(TuiEvent event) {
        {
            std::lock_guard<std::mutex> lock(events_mutex_);
            events_.push(std::move(event));
        }
        screen_.PostEvent(ftxui::Event::Custom);
    }
    void post(const SubAgentEvent& event) {
        switch (event.kind) {
            case SubAgentEventKind::Started:
                post(TuiEvent{SubAgentStarted{event.agent_id}});
                break;
            case SubAgentEventKind::StateChanged:
                post(TuiEvent{SubAgentStateChanged{event.agent_id, event.state}});
                break;
            case SubAgentEventKind::ToolStarted:
                post(TuiEvent{SubAgentStep{event.agent_id}});
                break;
            case SubAgentEventKind::Completed:
                post(TuiEvent{SubAgentEnded{event.agent_id, false}});
                break;
            case SubAgentEventKind::Failed:
                post(TuiEvent{SubAgentEnded{event.agent_id, true}});
                break;
        }
    }

    bool start_background(std::function<void()> fn) {
        if (busy_) return false;
        if (worker_.joinable()) worker_.join();
        busy_ = true;
        model_.begin_turn();
        worker_ = std::thread([fn = std::move(fn)] { fn(); });
        return true;
    }

    void request_approval(const ToolCall& call, std::shared_ptr<std::promise<bool>> approval) {
        post(ApprovalRequested{call, std::move(approval)});
    }

    void exit() {
        if (busy_.load()) {
            exit_requested_ = true;
            if (on_interrupt_) on_interrupt_();
            resolve_approval(false);
            return;
        }
        screen_.Exit();
    }

    void push_overlay(std::unique_ptr<Panel> panel) {
        overlay_stack_.push_back(std::move(panel));
    }
    void pop_overlay() { if (!overlay_stack_.empty()) overlay_stack_.pop_back(); }
    void open_help() { push_overlay(std::make_unique<HelpPanel>()); }
    void open_tools() { push_overlay(std::make_unique<ToolsPanel>()); }
    void open_memory() { push_overlay(std::make_unique<MemoryPanel>()); }
    void open_context() {
        auto panel = std::make_unique<ContextPanel>();
        panel->set_total_tokens(last_prompt_tokens_);
        panel->set_cumulative_input_tokens(total_input_tokens_);
        panel->set_total_output_tokens(total_output_tokens_);
        panel->set_last_prompt_tokens(last_prompt_tokens_);
        panel->set_has_usage(has_usage_ && !usage_missing_);
        panel->set_messages(messages_);
        panel->set_turns(completed_turns_);
        panel->set_tool_calls(tool_calls_);
        panel->set_system_prompt(system_prompt_);
        push_overlay(std::move(panel));
    }
    void open_model_selector() {
        auto selector = std::make_unique<ModelSelector>();
        selector->set_providers({"openai", "anthropic"});
        selector->set_models({"gpt-4o", "claude-opus-4-7", "deepseek-v4-pro"});
        selector->set_on_confirm([this](std::string, std::string) { pop_overlay(); });
        push_overlay(std::move(selector));
    }
    void open_command_palette() {
        auto palette = std::make_unique<CommandPalette>();
        palette->set_on_select([this](const commands::CommandMeta& cmd) { pending_command_ = cmd.name; });
        palette->set_on_cancel([this] { pop_overlay(); });
        push_overlay(std::move(palette));
    }

    bool handle_event(ftxui::Event event) {
        if (event == ftxui::Event::Custom) {
            drain_events();
            return true;
        }
        if (event.is_character() && event.character() == "\x03" && busy_.load()) {
            if (on_interrupt_) on_interrupt_();
            resolve_approval(false);
            post(Interrupted{});
            return true;
        }
        if (event.is_character() && event.character() == "\x04") { exit(); return true; }
        if (pending_approval_) {
            if (event == ftxui::Event::Escape) { resolve_approval(false); return true; }
            if (event.is_character() && (event.character() == "y" || event.character() == "Y")) {
                resolve_approval(true); return true;
            }
            if (event.is_character() && (event.character() == "n" || event.character() == "N")) {
                resolve_approval(false); return true;
            }
            return true;
        }
        if (event.is_character() && event.character() == "\x0F") { open_context(); return true; }
        if (event == ftxui::Event::F1) { open_help(); return true; }
        if (event == ftxui::Event::Escape && !overlay_stack_.empty()) { pop_overlay(); return true; }
        if (!overlay_stack_.empty()) {
            auto handled = overlay_stack_.back()->handle_event(event);
            if (pending_command_) {
                auto command = std::move(*pending_command_);
                pending_command_.reset();
                pop_overlay();
                if (on_command_) on_command_(std::move(command));
            }
            return handled;
        }
        if (!busy_ && event.is_character() && event.character() == "/" && model_.composer.empty()) {
            open_command_palette();
            return true;
        }
        if (event == ftxui::Event::ArrowUp && busy_.load() && model_.composer.empty()) {
            return model_.composer.edit_last_queued();
        }
        if (event == ftxui::Event::Backspace
            || (event.is_character() && (event.character() == "\x7F" || event.character() == "\x08"))) {
            model_.composer.backspace();
            return true;
        }
        if (event == ftxui::Event::Return) {
            auto kind = model_.composer.submit(busy_.load());
            if (kind == SubmitKind::Immediate) {
                auto input = model_.composer.take_immediate();
                model_.commit(std::make_unique<UserCell>(input));
                flush_committed();
                if (on_command_) on_command_(std::move(input));
            }
            return true;
        }
        if (event.is_character()) { model_.composer.append(event.character()); return true; }
        return false;
    }

    void run() {
        flush_committed();
        ticker_ = std::thread([this] {
            while (!stop_ticker_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                if (busy_.load()) screen_.RequestAnimationFrame();
            }
        });
        auto component = ftxui::Renderer([this] { return render_viewport(); });
        component |= ftxui::CatchEvent([this](ftxui::Event event) { return handle_event(event); });
        screen_.Loop(component);
    }
};

} // namespace merak::tui
