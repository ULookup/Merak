#pragma once
#include "../panel.hpp"
#include <string>
#include <vector>
#include <ftxui/dom/elements.hpp>

namespace merak::tui {

struct MemoryEntry {
    std::string type;
    std::string name;
    std::string summary;
};

class MemoryPanel : public Panel {
    std::vector<MemoryEntry> memories_;
    int selected_ = 0;
    std::string search_query_;
    bool memory_enabled_ = true;

public:
    void set_memories(const std::vector<MemoryEntry>& m) { memories_ = m; }
    void set_enabled(bool e) { memory_enabled_ = e; }

    bool is_overlay() const override { return true; }
    std::string title() const override { return "Memory"; }

    ftxui::Element render() override {
        using namespace ftxui;
        if (!memory_enabled_) {
            return vbox({
                text(" Memory is disabled") | dim,
                text(" Enable in settings.json to use this feature") | dim,
            }) | border | center;
        }
        Elements items;
        std::string current_type;
        for (size_t i = 0; i < memories_.size(); i++) {
            if (memories_[i].type != current_type) {
                current_type = memories_[i].type;
                items.push_back(text(" " + current_type + " ") | bold | color(Color::Palette256(178)));
            }
            auto line = text("  " + memories_[i].name);
            if (static_cast<int>(i) == selected_) {
                line = line | inverted;
            }
            items.push_back(line);
            items.push_back(text("    " + memories_[i].summary) | dim);
        }
        return vbox({
            text(" /" + search_query_) | inverted | border,
            vbox(std::move(items)) | flex | frame,
            separator(),
            text(" Enter: view  d: delete  Esc: back") | dim,
        }) | border | size(WIDTH, GREATER_THAN, 45);
    }

    bool handle_event(ftxui::Event event) override {
        using namespace ftxui;
        if (event == Event::Escape) return true;
        if (event == Event::ArrowDown && selected_ < static_cast<int>(memories_.size()) - 1) {
            selected_++; return true;
        }
        if (event == Event::ArrowUp && selected_ > 0) {
            selected_--; return true;
        }
        if (event == Event::Backspace ||
            (event.is_character() && event.character() == "\x7F") ||
            (event.is_character() && event.character() == "\x08")) {
            if (!search_query_.empty()) search_query_.pop_back();
            return true;
        }
        if (event.is_character()) {
            search_query_ += event.character();
            return true;
        }
        return false;
    }
};

} // namespace merak::tui
