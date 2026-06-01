#pragma once
#include "panel.hpp"
#include "components/status_bar.hpp"
#include "panels/command_palette.hpp"
#include "panels/help_panel.hpp"
#include "panels/context_panel.hpp"
#include "panels/model_selector.hpp"
#include "panels/tools_panel.hpp"
#include "panels/memory_panel.hpp"
#include "panels/chat_panel.hpp"
#include "../commands/command_registry.hpp"
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <queue>
#include <mutex>
#include <thread>
#include <future>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

namespace merak::tui {

class ScreenManager {
    std::unique_ptr<Panel> chat_panel_;
    std::vector<std::unique_ptr<Panel>> overlay_stack_;
    StatusBar status_bar_;
    ftxui::ScreenInteractive screen_;
    std::function<void(std::string)> on_command_;
    std::optional<std::string> pending_command_;
    std::queue<std::function<void()>> ui_events_;
    std::mutex ui_events_mutex_;
    std::thread worker_;
    bool busy_ = false;
    bool exit_requested_ = false;
    std::shared_ptr<std::promise<bool>> pending_approval_;
    int last_prompt_tokens_ = 0;
    int total_input_tokens_ = 0;
    int total_output_tokens_ = 0;
    int completed_turns_ = 0;
    int messages_ = 0;
    int tool_calls_ = 0;
    bool has_usage_ = false;
    bool usage_missing_ = false;
    std::string system_prompt_;

    ChatPanel& chat() { return static_cast<ChatPanel&>(*chat_panel_); }

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
        chat().clear_approval_prompt();
        status_bar_.set_state("Thinking...");
        approval->set_value(allowed);
    }

public:
    ScreenManager(std::unique_ptr<Panel> chat)
        : chat_panel_(std::move(chat))
        , screen_(ftxui::ScreenInteractive::Fullscreen())
    {}

    ~ScreenManager() {
        if (worker_.joinable()) worker_.join();
    }

    StatusBar& status_bar() { return status_bar_; }

    void set_on_command(std::function<void(std::string)> fn) { on_command_ = std::move(fn); }
    void set_system_prompt(std::string prompt) { system_prompt_ = std::move(prompt); }

    void exit() {
        if (busy_) {
            exit_requested_ = true;
            status_bar_.set_state("Finishing before exit...");
            return;
        }
        screen_.Exit();
    }

    void post(std::function<void()> fn) {
        {
            std::lock_guard<std::mutex> lock(ui_events_mutex_);
            ui_events_.push(std::move(fn));
        }
        screen_.PostEvent(ftxui::Event::Custom);
    }

    bool start_background(std::function<void()> fn) {
        if (busy_) return false;
        if (worker_.joinable()) worker_.join();
        busy_ = true;
        chat().set_busy(true);
        worker_ = std::thread([fn = std::move(fn)] { fn(); });
        return true;
    }

    void finish_background() {
        busy_ = false;
        chat().set_busy(false);
        if (exit_requested_) screen_.Exit();
    }

    void request_approval(const ToolCall& call, std::shared_ptr<std::promise<bool>> approval) {
        post([this, call, approval = std::move(approval)] {
            pending_approval_ = approval;
            auto detail = ChatPanel::summarize_tool(call);
            auto prompt = "? Allow " + call.name;
            if (!detail.empty()) prompt += ": " + detail;
            chat().set_approval_prompt(prompt + " ? [y/n]");
            status_bar_.set_state("Waiting for approval...");
        });
    }

    void record_usage(int input_tokens, int output_tokens, bool has_usage) {
        status_bar_.add_usage(input_tokens, output_tokens, has_usage);
        if (!has_usage) {
            usage_missing_ = true;
            return;
        }
        last_prompt_tokens_ = input_tokens;
        total_input_tokens_ += input_tokens;
        total_output_tokens_ += output_tokens;
        has_usage_ = true;
    }

    void record_turn_complete() {
        completed_turns_++;
        messages_ += 2;
    }

    void record_tool_start() { tool_calls_++; }
    void record_tool_end() { messages_++; }

    void push_overlay(std::unique_ptr<Panel> panel) {
        if (!overlay_stack_.empty()) overlay_stack_.back()->on_exit();
        overlay_stack_.push_back(std::move(panel));
        overlay_stack_.back()->on_enter();
    }

    void pop_overlay() {
        if (!overlay_stack_.empty()) {
            overlay_stack_.back()->on_exit();
            overlay_stack_.pop_back();
            if (!overlay_stack_.empty()) overlay_stack_.back()->on_enter();
        }
    }

    void open_command_palette() {
        auto palette = std::make_unique<CommandPalette>();
        palette->set_on_select([this](const commands::CommandMeta& cmd) {
            pending_command_ = cmd.name;
        });
        palette->set_on_cancel([this] { pop_overlay(); });
        push_overlay(std::move(palette));
    }

    void open_help() {
        push_overlay(std::make_unique<HelpPanel>());
    }

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
        auto sel = std::make_unique<ModelSelector>();
        sel->set_providers({"openai", "anthropic"});
        sel->set_models({"gpt-4o", "claude-opus-4-7", "deepseek-v4-pro"});
        sel->set_on_confirm([this](std::string, std::string) {
            pop_overlay();
        });
        push_overlay(std::move(sel));
    }

    void open_tools() {
        push_overlay(std::make_unique<ToolsPanel>());
    }

    void open_memory() {
        push_overlay(std::make_unique<MemoryPanel>());
    }

    bool handle_event(ftxui::Event event) {
        if (event == ftxui::Event::Custom) {
            drain_ui_events();
            return true;
        }
        if (pending_approval_) {
            if (event == ftxui::Event::Escape) {
                resolve_approval(false);
                return true;
            }
            if (event.is_character()) {
                auto character = event.character();
                if (character == "y" || character == "Y") {
                    resolve_approval(true);
                    return true;
                }
                if (character == "n" || character == "N") {
                    resolve_approval(false);
                    return true;
                }
            }
            return true;
        }
        // Ctrl+C or Ctrl+D: exit
        if (event.is_character() && (event.character() == "\x03" || event.character() == "\x04")) {
            exit();
            return true;
        }
        // Ctrl+O: open context panel
        if (event.is_character() && event.character() == "\x0F") {
            open_context();
            return true;
        }
        if (event == ftxui::Event::F1) {
            open_help();
            return true;
        }
        if (event == ftxui::Event::Escape) {
            if (!overlay_stack_.empty()) {
                pop_overlay();
                return true;
            }
        }
        if (!overlay_stack_.empty()) {
            bool handled = overlay_stack_.back()->handle_event(event);
            if (pending_command_) {
                auto command = std::move(*pending_command_);
                pending_command_.reset();
                pop_overlay();
                if (on_command_) on_command_(std::move(command));
            }
            return handled;
        }
        if (!busy_ && event.is_character() && event.character() == "/" && !chat_panel_->has_input()) {
            open_command_palette();
            return true;
        }
        return chat_panel_->handle_event(event);
    }

    void run() {
        auto component = ftxui::Renderer([&] {
            ftxui::Element content = chat_panel_->render();
            if (!overlay_stack_.empty()) {
                auto overlay = overlay_stack_.back()->render();
                content = ftxui::dbox({
                    content | ftxui::dim,
                    overlay | ftxui::clear_under | ftxui::center,
                });
            }
            return ftxui::vbox({
                content | ftxui::flex,
                status_bar_.render(),
            });
        });

        component |= ftxui::CatchEvent([this](ftxui::Event event) {
            return handle_event(event);
        });

        screen_.Loop(component);
    }
};

} // namespace merak::tui
