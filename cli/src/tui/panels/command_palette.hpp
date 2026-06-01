#pragma once
#include "../panel.hpp"
#include "../../commands/command_registry.hpp"
#include <functional>
#include <algorithm>
#include <ftxui/dom/elements.hpp>

namespace merak::tui {

class CommandPalette : public Panel {
    std::string query_;
    int selected_ = 0;
    std::vector<const commands::CommandMeta*> filtered_;
    std::function<void(const commands::CommandMeta&)> on_select_;
    std::function<void()> on_cancel_;

    void rebuild_filtered() {
        filtered_.clear();
        selected_ = 0;
        if (query_.empty()) {
            for (auto& cmd : commands::all_commands()) {
                filtered_.push_back(&cmd);
            }
        } else {
            filtered_ = commands::fuzzy_match(query_, 20);
        }
    }

public:
    CommandPalette() { rebuild_filtered(); }

    bool is_overlay() const override { return true; }
    std::string title() const override { return "Command Palette"; }

    void set_on_select(std::function<void(const commands::CommandMeta&)> fn) { on_select_ = std::move(fn); }
    void set_on_cancel(std::function<void()> fn) { on_cancel_ = std::move(fn); }

    ftxui::Element render() override {
        using namespace ftxui;
        Elements items;
        commands::CommandGroup last_group = commands::CommandGroup::Core;
        bool first = true;
        for (size_t i = 0; i < filtered_.size(); i++) {
            auto& cmd = *filtered_[i];
            if (first || cmd.group != last_group) {
                last_group = cmd.group;
                items.push_back(
                    text(" " + std::string(commands::group_name(cmd.group)) + " ")
                    | bold | color(Color::Palette256(178))
                );
                first = false;
            }
            auto line = text("  " + cmd.name);
            if (i == static_cast<size_t>(selected_)) {
                line = line | inverted;
            }
            items.push_back(line);
        }
        std::string hint;
        if (!filtered_.empty() && selected_ < static_cast<int>(filtered_.size())) {
            auto& cmd = *filtered_[selected_];
            hint = cmd.name;
            if (cmd.arg_hint) hint += " " + *cmd.arg_hint;
            hint += " — " + cmd.description;
        }
        return vbox({
            text(" /" + query_) | inverted | border,
            separator(),
            vbox(std::move(items)) | flex | frame,
            separator(),
            text(" " + hint) | dim,
        }) | border | size(WIDTH, GREATER_THAN, 50) | size(HEIGHT, GREATER_THAN, 10);
    }

    bool handle_event(ftxui::Event event) override {
        using namespace ftxui;
        if (event == Event::Escape) {
            if (on_cancel_) on_cancel_();
            return true;
        }
        if (event == Event::Return) {
            if (!filtered_.empty() && selected_ < static_cast<int>(filtered_.size())) {
                if (on_select_) on_select_(*filtered_[selected_]);
            }
            return true;
        }
        if (event == Event::ArrowDown || event == Event::Tab) {
            if (selected_ < static_cast<int>(filtered_.size()) - 1) selected_++;
            return true;
        }
        if (event == Event::ArrowUp || event == Event::TabReverse) {
            if (selected_ > 0) selected_--;
            return true;
        }
        if (event == Event::Backspace ||
            (event.is_character() && event.character() == "\x7F") ||
            (event.is_character() && event.character() == "\x08")) {
            if (!query_.empty()) { query_.pop_back(); rebuild_filtered(); }
            return true;
        }
        if (event.is_character()) {
            query_ += event.character();
            rebuild_filtered();
            return true;
        }
        return false;
    }
};

} // namespace merak::tui
