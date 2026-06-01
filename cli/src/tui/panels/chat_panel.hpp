#pragma once
#include "../panel.hpp"
#include "../colors.hpp"
#include <vector>
#include <string>
#include <functional>
#include <sstream>
#include <map>
#include <merak/message.hpp>
#include <nlohmann/json.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>

namespace merak::tui {

class ChatPanel : public Panel {
public:
    enum class LineStyle {
        Default,
        User,
        Assistant,
        LightGold,
        ToolRunning,
        Success,
        Error,
        Muted,
    };

    struct Segment {
        std::string text;
        LineStyle style = LineStyle::Default;
    };

private:
    struct TimelineLine {
        std::string text;
        LineStyle style = LineStyle::Default;
        std::vector<Segment> segments;
    };

    std::vector<TimelineLine> lines_;
    std::string input_buffer_;
    std::string prompt_ = "> ";
    std::function<void(std::string)> on_submit_;
    bool busy_ = false;
    bool assistant_active_ = false;
    bool assistant_rendered_ = false;
    size_t assistant_line_ = 0;
    std::map<std::string, size_t> tool_lines_;
    std::string approval_prompt_;

    static std::string truncate(std::string text, size_t max = 72) {
        if (text.size() <= max) return text;
        return text.substr(0, max - 3) + "...";
    }

    static ftxui::Element styled_text(const std::string& text, LineStyle style) {
        using namespace ftxui;
        auto element = ftxui::text(text);
        switch (style) {
            case LineStyle::User:        return element | color(colors::accent);
            case LineStyle::Assistant:   return element | color(colors::white);
            case LineStyle::LightGold:   return element | color(colors::light_gold);
            case LineStyle::ToolRunning: return element | color(colors::info);
            case LineStyle::Success:     return element | color(colors::success);
            case LineStyle::Error:       return element | color(colors::error);
            case LineStyle::Muted:       return element | color(colors::muted);
            case LineStyle::Default:     return element;
        }
        return element;
    }

public:
    void set_on_submit(std::function<void(std::string)> fn) { on_submit_ = std::move(fn); }
    void set_prompt(const std::string& p) { prompt_ = p; }

    void add_line(const std::string& line, LineStyle style = LineStyle::Default) {
        lines_.push_back({line, style, {}});
    }

    void add_segments(std::vector<Segment> segments) {
        lines_.push_back({"", LineStyle::Default, std::move(segments)});
    }

    void add_text(const std::string& text, LineStyle style = LineStyle::Default) {
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line)) {
            add_line(line, style);
        }
    }

    void set_busy(bool busy) {
        busy_ = busy;
        if (busy) assistant_rendered_ = false;
    }
    void set_approval_prompt(const std::string& prompt) { approval_prompt_ = prompt; }
    void clear_approval_prompt() { approval_prompt_.clear(); }

    void append_assistant_delta(const std::string& text) {
        if (text.empty()) return;
        assistant_rendered_ = true;
        if (!assistant_active_) {
            add_line("✦ ", LineStyle::Assistant);
            assistant_line_ = lines_.size() - 1;
            assistant_active_ = true;
        }
        for (char c : text) {
            if (c == '\n') {
                add_line("  ", LineStyle::Assistant);
                assistant_line_ = lines_.size() - 1;
            } else if (c != '\r') {
                lines_[assistant_line_].text.push_back(c);
            }
        }
    }

    void finish_assistant_response(const std::string& fallback = "") {
        if (!assistant_rendered_ && !fallback.empty()) add_text(fallback, LineStyle::Assistant);
        assistant_active_ = false;
    }

    void add_tool_start(const ToolCall& call) {
        finish_assistant_response();
        auto detail = summarize_tool(call);
        auto line = "◇ " + call.name;
        if (!detail.empty()) line += " " + detail;
        tool_lines_[call.id] = lines_.size();
        add_line(line, LineStyle::ToolRunning);
    }

    void finish_tool(const ToolResult& result) {
        auto it = tool_lines_.find(result.call_id);
        if (it == tool_lines_.end() || it->second >= lines_.size()) return;
        auto& line = lines_[it->second];
        line.text.replace(0, std::string("◇").size(), result.is_error ? "✗" : "✓");
        line.style = result.is_error ? LineStyle::Error : LineStyle::Success;
        if (result.is_error) {
            line.text += " failed";
            if (!result.output.empty()) {
                line.text += ": " + truncate(result.output, 60);
            }
        } else {
            line.text += " done";
        }
        tool_lines_.erase(it);
    }

    void add_turn_summary(int input_tokens, int output_tokens, int tools, bool has_usage) {
        auto usage = has_usage
            ? std::to_string(input_tokens) + " in · " + std::to_string(output_tokens) + " out"
            : "token usage n/a";
        add_line("─ " + usage + " · " + std::to_string(tools) + " tools", LineStyle::Muted);
        add_line("");
    }

    static std::string summarize_tool(const ToolCall& call) {
        try {
            auto args = nlohmann::json::parse(call.arguments.empty() ? "{}" : call.arguments);
            auto value = [&](const char* key) -> std::string {
                return args.contains(key) && args[key].is_string()
                    ? args[key].get<std::string>() : "";
            };
            if (call.name == "grep") {
                auto pattern = value("pattern");
                auto path = value("path");
                return truncate(pattern + (path.empty() ? "" : " in " + path));
            }
            for (auto* key : {"path", "command", "query", "pattern"}) {
                auto detail = value(key);
                if (!detail.empty()) return truncate(detail);
            }
        } catch (...) {
        }
        return "";
    }

    std::string title() const override { return "Chat"; }
    bool has_input() const override { return !input_buffer_.empty(); }

    ftxui::Element render() override {
        using namespace ftxui;
        Elements children;

        // Show last 50 lines max (scrollable)
        size_t start = lines_.size() > 50 ? lines_.size() - 50 : 0;
        for (size_t i = start; i < lines_.size(); i++) {
            auto& line = lines_[i];
            if (line.segments.empty()) {
                children.push_back(styled_text(line.text, line.style));
                continue;
            }
            Elements segments;
            for (auto& segment : line.segments) {
                segments.push_back(styled_text(segment.text, segment.style));
            }
            children.push_back(hbox(std::move(segments)));
        }

        if (!approval_prompt_.empty()) {
            children.push_back(text(approval_prompt_) | color(colors::accent) | bold);
        }

        auto input_line = busy_ ? "  Busy..." : prompt_ + input_buffer_ + " ";
        children.push_back(text(input_line) | inverted);

        return vbox(std::move(children));
    }

    bool handle_event(ftxui::Event event) override {
        using namespace ftxui;
        if (busy_) return true;
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
                auto input = std::move(input_buffer_);
                input_buffer_.clear();
                add_line("> " + input, LineStyle::User);
                on_submit_(std::move(input));
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
