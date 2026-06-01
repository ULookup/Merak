#pragma once
#include "../panel.hpp"
#include <vector>
#include <string>
#include <functional>
#include <ftxui/dom/elements.hpp>

namespace merak::tui {

class ModelSelector : public Panel {
    int selected_provider_ = 0;
    int selected_model_ = 0;
    std::vector<std::string> providers_;
    std::vector<std::string> models_;
    std::function<void(std::string, std::string)> on_confirm_;

public:
    void set_providers(const std::vector<std::string>& p) { providers_ = p; }
    void set_models(const std::vector<std::string>& m) { models_ = m; }
    void set_on_confirm(std::function<void(std::string, std::string)> fn) { on_confirm_ = std::move(fn); }

    bool is_overlay() const override { return true; }
    std::string title() const override { return "Model Selector"; }

    ftxui::Element render() override {
        using namespace ftxui;
        Elements provider_tabs;
        for (size_t i = 0; i < providers_.size(); i++) {
            auto tab = text(" " + providers_[i] + " ");
            if (static_cast<int>(i) == selected_provider_) {
                tab = tab | inverted;
            }
            provider_tabs.push_back(tab);
        }
        Elements model_list;
        for (size_t i = 0; i < models_.size(); i++) {
            auto item = text("  " + models_[i]);
            if (static_cast<int>(i) == selected_model_) {
                item = item | inverted;
            }
            model_list.push_back(item);
        }
        return vbox({
            hbox(std::move(provider_tabs)) | border,
            vbox(std::move(model_list)) | border | flex,
            text(" Enter: confirm  Esc: cancel") | dim,
        }) | border | size(WIDTH, GREATER_THAN, 30) | size(HEIGHT, GREATER_THAN, 10);
    }

    bool handle_event(ftxui::Event event) override {
        using namespace ftxui;
        if (event == Event::Escape) return true;
        if (event == Event::Return) {
            if (on_confirm_ && !models_.empty()) {
                on_confirm_(providers_[selected_provider_], models_[selected_model_]);
            }
            return true;
        }
        if (event == Event::ArrowDown) {
            if (selected_model_ < static_cast<int>(models_.size()) - 1) selected_model_++;
            return true;
        }
        if (event == Event::ArrowUp) {
            if (selected_model_ > 0) selected_model_--;
            return true;
        }
        if (event == Event::ArrowRight || event == Event::Tab) {
            if (selected_provider_ < static_cast<int>(providers_.size()) - 1) selected_provider_++;
            return true;
        }
        if (event == Event::ArrowLeft || event == Event::TabReverse) {
            if (selected_provider_ > 0) selected_provider_--;
            return true;
        }
        return false;
    }
};

} // namespace merak::tui
