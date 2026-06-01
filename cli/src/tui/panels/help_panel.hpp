#pragma once
#include "../panel.hpp"
#include "../../commands/command_registry.hpp"
#include <ftxui/dom/elements.hpp>

namespace merak::tui {

class HelpPanel : public Panel {
public:
    bool is_overlay() const override { return true; }
    std::string title() const override { return "Help"; }

    ftxui::Element render() override {
        using namespace ftxui;
        Elements groups;
        commands::CommandGroup last_group = commands::CommandGroup::Core;
        bool first = true;
        for (auto& cmd : commands::all_commands()) {
            if (first || cmd.group != last_group) {
                last_group = cmd.group;
                groups.push_back(separator());
                groups.push_back(
                    text(" " + std::string(commands::group_name(cmd.group)) + " ")
                    | bold | color(Color::Palette256(178))
                );
                first = false;
            }
            std::string line = "  " + cmd.name;
            if (cmd.arg_hint) line += " " + *cmd.arg_hint;
            groups.push_back(text(line));
            groups.push_back(text("    " + cmd.description) | dim);
        }
        groups.push_back(separator());
        groups.push_back(text(" Keyboard Shortcuts") | bold | color(Color::Palette256(178)));
        groups.push_back(text("  / Cmd palette    Ctrl+O Context    F1 Help    Ctrl+D Exit") | dim);
        return vbox(std::move(groups)) | border | frame | size(WIDTH, GREATER_THAN, 60);
    }

    bool handle_event(ftxui::Event event) override {
        return event == ftxui::Event::Escape;
    }
};

} // namespace merak::tui
