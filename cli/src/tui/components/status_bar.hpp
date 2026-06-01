#pragma once
#include <string>
#include <ftxui/dom/elements.hpp>

namespace merak::tui {

class StatusBar {
    std::string provider_ = "none";
    std::string model_ = "none";
    std::string token_info_ = "0/0";
public:
    void set_provider(const std::string& p) { provider_ = p; }
    void set_model(const std::string& m) { model_ = m; }
    void set_token_info(const std::string& t) { token_info_ = t; }

    ftxui::Element render() {
        using namespace ftxui;
        auto label = provider_ + " │ "
                    + model_ + " │ "
                    + token_info_;
        return text(label) | dim | borderLight | size(HEIGHT, EQUAL, 1);
    }
};

} // namespace merak::tui
