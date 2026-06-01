#pragma once
#include "panel.hpp"
#include "components/status_bar.hpp"
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
