#pragma once
#include <string>
#include <ftxui/dom/elements.hpp>

namespace merak::tui {

class StatusBar {
    std::string provider_ = "none";
    std::string model_ = "none";
    std::string permission_mode_ = "ask";
    int context_tokens_ = 0;
    int context_limit_ = 128000;
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
    void set_provider(const std::string& provider) { provider_ = provider; }
    void set_model(const std::string& model) { model_ = model; }
    void set_permission_mode(const std::string& mode) { permission_mode_ = mode; }
    void set_context_usage(int tokens, int limit = 128000) {
        context_tokens_ = tokens;
        context_limit_ = limit;
    }
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

    ftxui::Element render(size_t queued = 0) {
        using namespace ftxui;
        auto usage = has_exact_usage()
            ? "sum " + format_tokens(total_input_tokens_) + " in / "
                + format_tokens(total_output_tokens_) + " out"
            : "sum n/a";
        auto context = context_limit_ > 0
            ? std::to_string(context_tokens_ * 100 / context_limit_) + "% ctx"
            : "ctx n/a";
        auto queue = queued > 0 ? " | queued " + std::to_string(queued) : "";
        auto label = "/ commands | Ctrl+O context | " + provider_ + " | " + model_
            + " | " + permission_mode_ + " | " + context + queue + " | " + usage;
        return text(label) | dim | borderLight | size(HEIGHT, EQUAL, 1);
    }
};

} // namespace merak::tui
