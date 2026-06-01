#pragma once
#include "panels/chat_panel.hpp"
#include <string>

namespace merak::tui {

inline void add_welcome_banner(ChatPanel& chat,
                               const std::string& provider,
                               const std::string& model) {
    using Style = ChatPanel::LineStyle;
    auto styled = [&](const std::string& text, Style style) {
        chat.add_line(text, style);
    };
    auto framed = [&](const std::string& content, Style style) {
        chat.add_segments({
            {"│", Style::User},
            {content, style},
            {"│", Style::User},
        });
    };

    auto model_info = "   " + provider + " · " + model;
    auto model_info_width = 6 + provider.size() + model.size();
    if (model_info_width < 56) {
        model_info += std::string(56 - model_info_width, ' ');
    }

    chat.add_line("");
    styled("╭─ merak v0.2.0 ─────────────────────────────────────────╮", Style::User);
    framed("           ·              ✦              ·              ", Style::LightGold);
    framed("                         ╱╲                             ", Style::LightGold);
    framed("              ▟█▙      ╱  ╲      ▟█▙                    ", Style::LightGold);
    framed("          ▟███████▙  ╱ ✧  ╲  ▟███████▙                  ", Style::LightGold);
    framed("       ▟████████████████████████████████▙               ", Style::LightGold);
    framed("                                                        ", Style::Default);
    framed("   ███╗   ███╗███████╗██████╗  █████╗ ██╗  ██╗          ", Style::Assistant);
    framed("   ████╗ ████║██╔════╝██╔══██╗██╔══██╗██║ ██╔╝          ", Style::Assistant);
    framed("   ██╔████╔██║█████╗  ██████╔╝███████║█████╔╝           ", Style::Assistant);
    framed("   ██║╚██╔╝██║██╔══╝  ██╔══██╗██╔══██║██╔═██╗           ", Style::Assistant);
    framed("   ██║ ╚═╝ ██║███████╗██║  ██║██║  ██║██║  ██╗          ", Style::Assistant);
    framed("                                                        ", Style::Default);
    framed(model_info, Style::Muted);
    framed("   ─────────────────────────────────────────────────    ", Style::Muted);
    framed("   Tips                                                 ", Style::Muted);
    framed("   /        command palette     F1       help           ", Style::Muted);
    framed("   Ctrl+O   context usage       /exit    leave session  ", Style::Muted);
    styled("╰────────────────────────────────────────────────────────╯", Style::User);
    chat.add_line("");
}

} // namespace merak::tui
