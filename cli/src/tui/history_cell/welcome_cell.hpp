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

    std::vector<std::string> render(size_t width) const override {
        std::vector<std::string> lines;

        // figlet title
        for (const auto& row : figlet) {
            lines.push_back(ansi(theme::ANSI_ACCENT, row));
        }

        // box width: fit terminal minus margin, but at least large enough for content
        auto info_line = "agent " + version_ + " · model " + model_ + " · branch " + branch_;
        static constexpr const char* tips_line =
            "/help commands · Shift+Enter newline · Ctrl+T tools · Ctrl+O transcript";
        auto content_width = std::max(info_line.size(), std::strlen(tips_line)) + 4; // 2 padding each side
        auto box_width = width > 2 ? std::min(width - 2, content_width) : content_width;

        auto pad_right = [box_width](const std::string& text) {
            auto inner = box_width > 4 ? box_width - 4 : 0;
            if (text.size() >= inner) return text;
            return text + repeat_text(" ", inner - text.size());
        };

        // top border
        auto safe_box = std::max(box_width, size_t{2});
        lines.push_back(ansi(theme::ANSI_DIM,
            "┌" + repeat_text("─", safe_box - 2) + "┐"));

        // info row
        lines.push_back(ansi(theme::ANSI_DIM, "│ ")
            + ansi(theme::ANSI_FG, pad_right(info_line))
            + ansi(theme::ANSI_DIM, " │"));

        // tips row
        auto tips_styled = std::string(tips_line);
        lines.push_back(ansi(theme::ANSI_DIM, "│ ")
            + ansi(theme::ANSI_DIM, pad_right(tips_styled))
            + ansi(theme::ANSI_DIM, " │"));

        // bottom border
        lines.push_back(ansi(theme::ANSI_DIM,
            "└" + repeat_text("─", safe_box - 2) + "┘"));

        return lines;
    }

    nlohmann::json to_json() const override {
        return {{"type", "welcome"}};
    }
};

} // namespace merak::tui
