#pragma once
#include "command_registry.hpp"
#include "../output/cli_output.hpp"

namespace merak::commands {

inline void format_not_found_error(const std::string& entity_type,
                                    const std::string& name,
                                    const std::vector<const CommandMeta*>& suggestions,
                                    const std::string& hint_cmd = "") {
    cli::err(entity_type + " '" + name + "' not found");

    if (!suggestions.empty()) {
        std::string suggestion_text;
        for (size_t i = 0; i < std::min(suggestions.size(), size_t(3)); i++) {
            if (i > 0) suggestion_text += ", ";
            suggestion_text += suggestions[i]->name;
        }
        cli::dim("Did you mean: " + suggestion_text);
    }

    if (!hint_cmd.empty()) {
        cli::dim("Try: " + hint_cmd);
    }
}

} // namespace merak::commands
