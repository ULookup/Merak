#pragma once
#include "../panel.hpp"
#include <string>
#include <ftxui/dom/elements.hpp>

namespace merak::tui {

class ContextPanel : public Panel {
    int total_tokens_ = 0;
    int max_tokens_ = 128000;
    int messages_ = 0;
    int turns_ = 0;
    int tool_calls_ = 0;
    std::string system_prompt_preview_;

public:
    void set_total_tokens(int t) { total_tokens_ = t; }
    void set_max_tokens(int m) { max_tokens_ = m; }
    void set_messages(int m) { messages_ = m; }
    void set_turns(int t) { turns_ = t; }
    void set_tool_calls(int c) { tool_calls_ = c; }
    void set_system_prompt(const std::string& s) { system_prompt_preview_ = s; }

    bool is_overlay() const override { return true; }
    std::string title() const override { return "Context"; }

    ftxui::Element render() override {
        using namespace ftxui;
        int pct = max_tokens_ > 0 ? (total_tokens_ * 100 / max_tokens_) : 0;
        int bar_width = pct * 30 / 100;
        return vbox({
            text(" Token Usage") | bold | color(Color::Palette256(178)),
            hbox({
                text(" ") | size(WIDTH, EQUAL, 1),
                text(std::string(bar_width, '#')) | color(Color::Palette256(178)),
                text(std::string(30 - bar_width, '-')) | dim,
                text(" " + std::to_string(pct) + "% (" + std::to_string(total_tokens_) + "/" + std::to_string(max_tokens_) + ")"),
            }),
            separator(),
            text(" Session") | bold | color(Color::Palette256(178)),
            text("  Messages: " + std::to_string(messages_)),
            text("  Turns: " + std::to_string(turns_)),
            text("  Tool calls: " + std::to_string(tool_calls_)),
            separator(),
            text(" System Prompt") | bold | color(Color::Palette256(178)),
            text("  " + (system_prompt_preview_.empty() ? "(none)" : system_prompt_preview_.substr(0, 120) + "...")),
        }) | border | size(WIDTH, GREATER_THAN, 40);
    }

    bool handle_event(ftxui::Event event) override {
        return event == ftxui::Event::Escape;
    }
};

} // namespace merak::tui
