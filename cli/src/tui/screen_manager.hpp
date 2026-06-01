#pragma once
#include "panel.hpp"
#include "components/status_bar.hpp"
#include "panels/command_palette.hpp"
#include "panels/help_panel.hpp"
#include "panels/context_panel.hpp"
#include "panels/model_selector.hpp"
#include "panels/tools_panel.hpp"
#include "panels/memory_panel.hpp"
#include "../commands/command_registry.hpp"
#include <vector>
#include <memory>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

namespace merak::tui {

class ScreenManager {
    std::unique_ptr<Panel> chat_panel_;
    std::vector<std::unique_ptr<Panel>> overlay_stack_;
    StatusBar status_bar_;
    ftxui::ScreenInteractive screen_;

public:
    ScreenManager(std::unique_ptr<Panel> chat)
        : chat_panel_(std::move(chat))
        , screen_(ftxui::ScreenInteractive::Fullscreen())
    {}

    StatusBar& status_bar() { return status_bar_; }

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
            if (cmd.name == "/help") {
                pop_overlay(); // close palette
                open_help();
            }
            // other command dispatch handled by caller
        });
        palette->set_on_cancel([this] { pop_overlay(); });
        push_overlay(std::move(palette));
    }

    void open_help() {
        push_overlay(std::make_unique<HelpPanel>());
    }

    void open_context() {
        push_overlay(std::make_unique<ContextPanel>());
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
            // Ctrl+O: open context panel
            if (event.is_character() && event.character() == "\x0F") {
                open_context();
                return true;
            }
            if (event == ftxui::Event::Escape) {
                if (!overlay_stack_.empty()) {
                    pop_overlay();
                    return true;
                }
            }
            if (!overlay_stack_.empty()) {
                return overlay_stack_.back()->handle_event(event);
            }
            return chat_panel_->handle_event(event);
        });

        screen_.Loop(component);
    }
};

} // namespace merak::tui
