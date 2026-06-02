#pragma once
#include "chat_model.hpp"
#include <memory>
#include <string>
#include <vector>

namespace merak::tui {

inline void add_welcome_banner(ChatModel& chat,
                               const std::string& provider,
                               const std::string& model) {
    auto model_info = "   " + provider + " · " + model;
    auto model_info_width = 6 + provider.size() + model.size();
    if (model_info_width < 56) {
        model_info += std::string(56 - model_info_width, ' ');
    }

    chat.commit(std::make_unique<RawCell>(std::vector<std::string>{
        "",
        "╭─ merak v0.2.0 ─────────────────────────────────────────╮",
        "│           ·              ✦              ·              │",
        "│                         ╱╲                             │",
        "│              ▟█▙      ╱  ╲      ▟█▙                    │",
        "│          ▟███████▙  ╱ ✧  ╲  ▟███████▙                  │",
        "│       ▟████████████████████████████████▙               │",
        "│                                                        │",
        "│   ███╗   ███╗███████╗██████╗  █████╗ ██╗  ██╗          │",
        "│   ████╗ ████║██╔════╝██╔══██╗██╔══██╗██║ ██╔╝          │",
        "│   ██╔████╔██║█████╗  ██████╔╝███████║█████╔╝           │",
        "│   ██║╚██╔╝██║██╔══╝  ██╔══██╗██╔══██║██╔═██╗           │",
        "│   ██║ ╚═╝ ██║███████╗██║  ██║██║  ██║██║  ██╗          │",
        "│                                                        │",
        "│" + model_info + "│",
        "│   ─────────────────────────────────────────────────    │",
        "│   Tips                                                 │",
        "│   /        command palette     F1       help           │",
        "│   Ctrl+O   context usage       /exit    leave session  │",
        "╰────────────────────────────────────────────────────────╯",
        "",
    }));
}

} // namespace merak::tui
