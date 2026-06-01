#pragma once
#include <string>
#include <ftxui/dom/elements.hpp>

namespace merak::tui {

class Panel {
public:
    virtual ~Panel() = default;
    virtual std::string title() const = 0;
    virtual ftxui::Element render() = 0;
    virtual bool handle_event(ftxui::Event event) = 0;
    virtual void on_enter() {}
    virtual void on_exit() {}
    virtual bool is_overlay() const { return false; }
};

} // namespace merak::tui
