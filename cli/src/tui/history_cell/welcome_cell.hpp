#pragma once
#include "history_cell.hpp"
#include "../../theme/theme.hpp"
#include <cstring>

namespace merak::tui {

class WelcomeCell final : public HistoryCell {
    std::string version_;
    std::string model_;
    std::string branch_;

    static constexpr const char* figlet[6] = {
        "███╗ ███╗██████╗ █████╗ ██╗  ██╗",
        "████╗████║██╔══██╗██╔══██╗██║ ██╔╝",
        "██╔████╔██║██████╔╝███████║█████╔╝ ",
        "██║╚██╔╝██║██╔══██╗██╔══██║██╔═██╗",
        "██║ ╚═╝ ██║██║  ██║██║  ██║██║  ██╗",
        "╚╝     ╚╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═╝",
    };

public:
    WelcomeCell(std::string version, std::string model, std::string branch)
        : version_(std::move(version)), model_(std::move(model)), branch_(std::move(branch)) {}

    void render(Buffer& buf, uint16_t width) const override {
        auto& t = theme::active_theme();
        Style accent; accent.fg = t.accent;
        Style dim_fg; dim_fg.fg = t.dim; dim_fg.dim(true);
        Style base;

        std::vector<std::vector<Span>> lines;

        // figlet title
        for (const auto& row : figlet) {
            lines.push_back({Span{row, accent}});
        }

        auto info_line = "agent " + version_ + " · model " + model_ + " · branch " + branch_;
        static constexpr const char* tips_line =
            "/help commands · Shift+Enter newline · Ctrl+T tools · Ctrl+O transcript";
        auto content_width = std::max(info_line.size(), std::strlen(tips_line)) + 4;
        auto box_width = width > 2 ? std::min(width - 2, content_width) : content_width;

        auto pad_right = [box_width](const std::string& text) {
            auto inner = box_width > 4 ? box_width - 4 : 0;
            if (text.size() >= inner) return text;
            return text + repeat_text(" ", inner - text.size());
        };

        auto safe_box = std::max(box_width, size_t{2});
        lines.push_back({Span{"┌" + repeat_text("─", safe_box - 2) + "┐", dim_fg}});
        lines.push_back({Span{"│ ", dim_fg}, Span{pad_right(info_line), base}, Span{" │", dim_fg}});
        lines.push_back({Span{"│ ", dim_fg}, Span{pad_right(std::string(tips_line)), dim_fg}, Span{" │", dim_fg}});
        lines.push_back({Span{"└" + repeat_text("─", safe_box - 2) + "┘", dim_fg}});

        if (buf.h < lines.size()) buf.resize(buf.w, lines.size());
        write_spans(buf, lines);
    }

    nlohmann::json to_json() const override {
        return {{"type", "welcome"}};
    }
};

} // namespace merak::tui
