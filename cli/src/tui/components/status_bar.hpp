#pragma once
#include "../colors.hpp"
#include "../history_cell/history_cell.hpp"
#include <cstdio>
#include <chrono>
#include <string>
#include <vector>
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
    std::string git_branch_;
    std::string cwd_;
    std::string permission_mode_ = "Prompt";
    int pending_approvals_ = 0;
    int running_agents_ = 0;
    int token_budget_limit_ = 0;
    int token_budget_used_ = 0;
    double estimated_cost_ = 0.0;

    static std::string format_tokens(int tokens) {
        if (tokens < 1000) return std::to_string(tokens);
        auto whole = tokens / 1000;
        auto tenth = (tokens % 1000) / 100;
        return std::to_string(whole) + "." + std::to_string(tenth) + "k";
    }
    static std::string spinner_prefix(const std::string& state) {
        if (state == "Idle" || state == "Cancelled") return state;
        static constexpr const char* frames[] = {"✶", "✸", "✹", "✺"};
        const auto tick = static_cast<size_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count() / 500);
        return std::string(frames[tick % 4]) + " " + state;
    }

public:
    void set_provider(const std::string& p) { provider_ = p; }
    void set_model(const std::string& m) { model_ = m; }
    void set_state(const std::string& state) { state_ = state; }
    void set_git_branch(std::string branch) { git_branch_ = std::move(branch); }
    const std::string& git_branch() const { return git_branch_; }
    const std::string& cwd() const { return cwd_; }
    void set_cwd(std::string cwd) { cwd_ = std::move(cwd); }
    void set_permission_mode(std::string mode) { permission_mode_ = std::move(mode); }
    void set_pending_approvals(int count) { pending_approvals_ = count; }
    void set_running_agents(int count) { running_agents_ = count; }
    void set_token_budget(int limit, int used) { token_budget_limit_ = limit; token_budget_used_ = used; }
    void set_estimated_cost(double value) { estimated_cost_ = value; }
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
    const std::string& state() const { return state_; }

    static std::string truncate_path(std::string path, size_t max = 28) {
        if (path.size() <= max) return path;
        auto tail = max > 4 ? max - 4 : max;
        return "..." + path.substr(path.size() - tail);
    }
    int token_budget_percent() const {
        if (token_budget_limit_ <= 0) return 0;
        return static_cast<int>((100LL * token_budget_used_) / token_budget_limit_);
    }
    std::string plain_text(size_t queued = 0, size_t width = 0) const {
        auto usage = has_exact_usage()
            ? "Σ " + format_tokens(total_input_tokens_) + " in / "
                + format_tokens(total_output_tokens_) + " out"
            : "Σ n/a";
        std::vector<std::string> chips{provider_, model_, spinner_prefix(state_), usage};
        if (!git_branch_.empty()) chips.push_back("branch " + git_branch_);
        if (!cwd_.empty()) chips.push_back(truncate_path(cwd_));
        if (token_budget_limit_ > 0) chips.push_back(std::to_string(token_budget_percent()) + "%");
        if (estimated_cost_ > 0.0) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "$%.2f", estimated_cost_);
            chips.push_back(buffer);
        }
        if (!permission_mode_.empty()) chips.push_back(permission_mode_);
        if (pending_approvals_ > 0) chips.push_back("pending " + std::to_string(pending_approvals_));
        if (running_agents_ > 0) chips.push_back("agents " + std::to_string(running_agents_));
        if (queued > 0) chips.push_back("queued " + std::to_string(queued));
        auto join = [&] {
            std::string out;
            for (size_t i = 0; i < chips.size(); ++i) {
                if (i > 0) out += " │ ";
                out += chips[i];
            }
            return out;
        };
        auto text = join();
        while (width > 0 && text.size() > width && chips.size() > 3) {
            chips.erase(chips.end() - 1);
            text = join();
        }
        if (width > 0 && text.size() > width) text = truncate_text(text, width);
        return text;
    }

    ftxui::Element render() {
        using namespace ftxui;
        auto usage = has_exact_usage()
            ? "Σ " + format_tokens(total_input_tokens_) + " in / "
                + format_tokens(total_output_tokens_) + " out"
            : "Σ n/a";
        auto state_color = colors::info;
        if (state_ == "Idle") state_color = colors::muted;
        if (state_ == "Error") state_color = colors::error;
        if (state_ == "Waiting for approval...") state_color = colors::accent;
        return hbox({
            text(provider_ + " │ " + model_ + " │ ") | color(colors::muted),
            text(spinner_prefix(state_)) | color(state_color),
            text(" │ " + usage) | color(colors::muted),
        }) | borderLight | size(HEIGHT, EQUAL, 1);
    }
};

} // namespace merak::tui
