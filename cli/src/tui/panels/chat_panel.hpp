#pragma once
#include "../panel.hpp"
#include <vector>
#include <string>
#include <functional>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>

namespace merak::tui {

class ChatPanel : public Panel {
    std::vector<std::string> lines_;
    std::string input_buffer_;
    std::string prompt_ = "> ";
    std::function<void(std::string)> on_submit_;

public:
    void set_on_submit(std::function<void(std::string)> fn) { on_submit_ = std::move(fn); }
    void set_prompt(const std::string& p) { prompt_ = p; }

    void add_line(const std::string& line) {
        lines_.push_back(line);
    }

    std::string title() const override { return "Chat"; }

    ftxui::Element render() override {
        using namespace ftxui;
        Elements children;

        // Show last 50 lines max (scrollable)
        size_t start = lines_.size() > 50 ? lines_.size() - 50 : 0;
        for (size_t i = start; i < lines_.size(); i++) {
            children.push_back(text(lines_[i]));
        }

        // Input line at bottom
        children.push_back(text(prompt_ + input_buffer_ + " ") | inverted);

        return vbox(std::move(children));
    }

    bool handle_event(ftxui::Event event) override {
        using namespace ftxui;
        // Backspace must be checked BEFORE is_character() — terminals send
        // DEL (0x7F) which FTXUI sometimes reports as a character, not a key.
        if (event == Event::Backspace ||
            (event.is_character() && event.character() == "\x7F") ||
            (event.is_character() && event.character() == "\x08")) {
            if (!input_buffer_.empty()) input_buffer_.pop_back();
            return true;
        }
        if (event == Event::Return) {
            if (on_submit_ && !input_buffer_.empty()) {
                on_submit_(input_buffer_);
                lines_.push_back("> " + input_buffer_);
                input_buffer_.clear();
            }
            return true;
        }
        if (event.is_character()) {
            input_buffer_ += event.character();
            return true;
        }
        return false;
    }
};

} // namespace merak::tui
