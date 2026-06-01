#pragma once
#include "panels/chat_panel.hpp"
#include <string>

namespace merak::tui {

inline void add_welcome_banner(ChatPanel& chat,
                               const std::string& provider,
                               const std::string& model) {
    auto model_info = "   " + provider + " · " + model;
    auto model_info_width = 6 + provider.size() + model.size();
    if (model_info_width < 56) {
        model_info += std::string(56 - model_info_width, ' ');
    }

    chat.add_line("");
    chat.add_line("╭─ merak v0.2.0 ─────────────────────────────────────────╮");
    chat.add_line("│           ·              ✦              ·              │");
    chat.add_line("│                         ╱╲                             │");
    chat.add_line("│              ▟█▙      ╱  ╲      ▟█▙                    │");
    chat.add_line("│          ▟███████▙  ╱ ✧  ╲  ▟███████▙                  │");
    chat.add_line("│       ▟████████████████████████████████▙               │");
    chat.add_line("│                                                        │");
    chat.add_line("│   ███╗   ███╗███████╗██████╗  █████╗ ██╗  ██╗          │");
    chat.add_line("│   ████╗ ████║██╔════╝██╔══██╗██╔══██╗██║ ██╔╝          │");
    chat.add_line("│   ██╔████╔██║█████╗  ██████╔╝███████║█████╔╝           │");
    chat.add_line("│   ██║╚██╔╝██║██╔══╝  ██╔══██╗██╔══██║██╔═██╗           │");
    chat.add_line("│   ██║ ╚═╝ ██║███████╗██║  ██║██║  ██║██║  ██╗          │");
    chat.add_line("│                                                        │");
    chat.add_line("│" + model_info + "│");
    chat.add_line("│   ─────────────────────────────────────────────────    │");
    chat.add_line("│   Tips                                                 │");
    chat.add_line("│   /        command palette     F1       help           │");
    chat.add_line("│   Ctrl+O   context usage       /exit    leave session  │");
    chat.add_line("╰────────────────────────────────────────────────────────╯");
    chat.add_line("");
}

} // namespace merak::tui
