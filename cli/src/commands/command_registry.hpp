#pragma once
#include <string>
#include <vector>
#include <optional>
#include <utility>

namespace merak::commands {

enum class CommandGroup {
    Core,
    Session,
    Tools,
    Config,
    Memory,
    System,
    Mcp,
};

const char* group_name(CommandGroup g);

struct SubCommand {
    std::string token;
    std::string description;
};

struct CommandMeta {
    std::string name;
    std::string description;
    CommandGroup group;
    std::vector<SubCommand> subcommands;
    std::optional<std::string> arg_hint;
    std::vector<std::string> usage;
};

// Global registry of all slash commands
const std::vector<CommandMeta>& all_commands();

// Look up a single command by exact name
const CommandMeta* find_command(const std::string& name);

// Resolve prefix to command; if ambiguous returns vector of candidates
std::pair<const CommandMeta*, std::vector<const CommandMeta*>>
resolve_command(const std::string& input);

// Fuzzy match returning scored candidates
std::vector<const CommandMeta*> fuzzy_match(const std::string& input, size_t limit = 5);

// Get subcommand completions for a parent command
const std::vector<SubCommand>* subcommand_completions(const std::string& parent);

// Completion candidates for a prefix
std::vector<std::pair<std::string, std::string>>
completion_candidates(const std::string& prefix);

} // namespace merak::commands
