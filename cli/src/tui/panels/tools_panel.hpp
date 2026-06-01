#pragma once
#include "../panel.hpp"
#include <string>
#include <vector>
#include <ftxui/dom/elements.hpp>

namespace merak::tui {

struct ToolEntry {
    std::string name;
    std::string source;
    std::string description;
};

class ToolsPanel : public Panel {
    std::vector<ToolEntry> tools_;
    int selected_ = 0;

public:
    void set_tools(const std::vector<ToolEntry>& t) { tools_ = t; }

    bool is_overlay() const override { return true; }
    std::string title() const override { return "Tools"; }

    ftxui::Element render() override {
        using namespace ftxui;
        std::string current_source;
        Elements items;
        for (size_t i = 0; i < tools_.size(); i++) {
            if (tools_[i].source != current_source) {
                current_source = tools_[i].source;
                items.push_back(text(" " + current_source + " ") | bold | color(Color::Palette256(178)));
            }
            auto line = text("  " + tools_[i].name);
            if (static_cast<int>(i) == selected_) {
                line = line | inverted;
            }
            items.push_back(line);
        }
        return vbox({
            vbox(std::move(items)) | flex | frame,
            separator(),
            text(" Enter: detail  Esc: back  ") | dim,
        }) | border | size(WIDTH, GREATER_THAN, 30);
    }

    bool handle_event(ftxui::Event event) override {
        using namespace ftxui;
        if (event == Event::Escape) return true;
        if (event == Event::ArrowDown && selected_ < static_cast<int>(tools_.size()) - 1) {
            selected_++; return true;
        }
        if (event == Event::ArrowUp && selected_ > 0) {
            selected_--; return true;
        }
        return false;
    }
};

} // namespace merak::tui
