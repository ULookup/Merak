#pragma once
#include <string>
#include <ftxui/dom/elements.hpp>

namespace merak::tui {

class StatusBar {
    std::string provider_ = "none";
    std::string model_ = "none";
    std::string state_ = "Idle";
    int total_input_tokens_ = 0;
    int total_output_tokens_ = 0;
    bool has_usage_ = false;
    bool usage_missing_ = false;

    static std::string format_tokens(int tokens) {
        if (tokens < 1000) return std::to_string(tokens);
        auto whole = tokens / 1000;
        auto tenth = (tokens % 1000) / 100;
        return std::to_string(whole) + "." + std::to_string(tenth) + "k";
    }

public:
    void set_provider(const std::string& p) { provider_ = p; }
    void set_model(const std::string& m) { model_ = m; }
    void set_state(const std::string& state) { state_ = state; }
    void add_usage(int input_tokens, int output_tokens, bool has_usage) {
        if (!has_usage) {
            usage_missing_ = true;
            return;
        }
        total_input_tokens_ += input_tokens;
        total_output_tokens_ += output_tokens;
        has_usage_ = true;
    }
    int total_input_tokens() const { return total_input_tokens_; }
    int total_output_tokens() const { return total_output_tokens_; }
    bool has_usage() const { return has_usage_; }
    bool has_exact_usage() const { return has_usage_ && !usage_missing_; }

    ftxui::Element render() {
        using namespace ftxui;
        auto usage = has_exact_usage()
            ? "Σ " + format_tokens(total_input_tokens_) + " in / "
                + format_tokens(total_output_tokens_) + " out"
            : "Σ n/a";
        auto label = provider_ + " │ " + model_ + " │ " + state_ + " │ " + usage;
        return text(label) | dim | borderLight | size(HEIGHT, EQUAL, 1);
    }
};

} // namespace merak::tui
