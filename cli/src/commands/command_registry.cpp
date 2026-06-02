#include "command_registry.hpp"
#include <algorithm>
#include <cctype>

namespace merak::commands {

const char* group_name(CommandGroup g) {
    switch (g) {
        case CommandGroup::Core:    return "Core";
        case CommandGroup::Session: return "Session";
        case CommandGroup::Tools:   return "Tools";
        case CommandGroup::Config:  return "Config";
        case CommandGroup::Memory:  return "Memory";
        case CommandGroup::System:  return "System";
        case CommandGroup::Mcp:     return "MCP";
    }
    return "Unknown";
}

const std::vector<CommandMeta>& all_commands() {
    static const std::vector<CommandMeta> cmds = {
        // ── Core ──
        CommandMeta{
            "/help", "Show available commands; /help keys for shortcuts",
            CommandGroup::Core, {}, {}, {}
        },
        CommandMeta{
            "/model", "Open the model picker, show current model, or switch",
            CommandGroup::Core,
            {{"info", "Show current model"}, {"list", "Choose a model"}, {"clear", "Reset to default"}},
            "[info | list | clear | <name>]", {}
        },
        CommandMeta{
            "/clear", "Start a new session",
            CommandGroup::Core, {}, {}, {}
        },
        CommandMeta{
            "/context", "Show context and token usage",
            CommandGroup::Core, {}, {}, {}
        },
        CommandMeta{
            "/exit", "Exit Merak",
            CommandGroup::Core, {}, {}, {}
        },
        CommandMeta{
            "/quit", "Exit Merak (alias for /exit)",
            CommandGroup::Core, {}, {}, {}
        },

        // ── Session ──
        CommandMeta{
            "/session", "Session management",
            CommandGroup::Session,
            {{"list", "List sessions"}, {"new", "Create a new session"}, {"use", "Switch to a session"}},
            "[list | new | use <id>]", {}
        },
        CommandMeta{
            "/transcript", "Browse the current session transcript",
            CommandGroup::Session, {}, {}, {}
        },

        // ── Tools ──
        CommandMeta{
            "/tools", "Show loaded tools and MCP status",
            CommandGroup::Tools, {}, {}, {}
        },
        CommandMeta{
            "/tool-calls", "Browse tool calls and full output",
            CommandGroup::Tools, {}, {}, {}
        },
        CommandMeta{
            "/mcp", "MCP server management",
            CommandGroup::Tools,
            {{"list", "List servers"}, {"tools", "List MCP tools"}, {"status", "Server status"}},
            "[list | tools | status]", {}
        },

        // ── Config ──
        CommandMeta{
            "/config", "Show or edit configuration",
            CommandGroup::Config,
            {{"show", "Show current config"}, {"paths", "Show config file paths"}},
            "[show | paths]", {}
        },

        // ── Memory ──
        CommandMeta{
            "/memory", "Browse and search memories",
            CommandGroup::Memory,
            {{"list", "List memories"}, {"search", "Search memories"}, {"stats", "Memory stats"}},
            "[list | search <q> | stats]", {}
        },

        // ── System ──
        CommandMeta{
            "/allow", "Permission mode: auto, plan, prompt, deny",
            CommandGroup::System,
            {{"auto", "Auto-approve"}, {"plan", "Read-only"}, {"prompt", "Ask before"}, {"deny", "Deny all"}},
            "[auto | plan | prompt | deny]", {}
        },
    };
    return cmds;
}

const CommandMeta* find_command(const std::string& name) {
    for (auto& cmd : all_commands()) {
        if (cmd.name == name) return &cmd;
    }
    return nullptr;
}

static std::string lower_str(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

static int fuzzy_score(const std::string& cmd, const std::string& input) {
    auto lower_cmd = lower_str(cmd);
    auto lower_input = lower_str(input);

    if (lower_cmd == lower_input) return 20000;
    if (lower_cmd.starts_with(lower_input)) return 10000 + (100 - std::min(static_cast<int>(lower_cmd.size()), 100));
    if (lower_cmd.find(lower_input) != std::string::npos) return 5000;

    int score = 0;
    size_t pos = 0;
    for (char c : lower_input) {
        pos = lower_cmd.find(c, pos);
        if (pos == std::string::npos) return 0;
        score += 10;
        pos++;
    }
    return 1000 + score;
}

std::pair<const CommandMeta*, std::vector<const CommandMeta*>>
resolve_command(const std::string& input) {
    std::vector<const CommandMeta*> matches;
    for (auto& cmd : all_commands()) {
        if (cmd.name.starts_with(input)) {
            matches.push_back(&cmd);
        }
    }
    if (matches.size() == 1) return {matches[0], {}};
    return {nullptr, matches};
}

std::vector<const CommandMeta*> fuzzy_match(const std::string& input, size_t limit) {
    std::vector<std::pair<int, const CommandMeta*>> scored;
    for (auto& cmd : all_commands()) {
        int s = fuzzy_score(cmd.name, input);
        if (s > 0) scored.push_back({s, &cmd});
    }
    std::sort(scored.begin(), scored.end(),
              [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<const CommandMeta*> result;
    for (size_t i = 0; i < std::min(limit, scored.size()); i++) {
        result.push_back(scored[i].second);
    }
    return result;
}

const std::vector<SubCommand>* subcommand_completions(const std::string& parent) {
    auto* cmd = find_command(parent);
    if (cmd && !cmd->subcommands.empty()) return &cmd->subcommands;
    return nullptr;
}

std::vector<std::pair<std::string, std::string>>
completion_candidates(const std::string& prefix) {
    std::vector<std::pair<std::string, std::string>> result;
    for (auto& cmd : all_commands()) {
        if (cmd.name.starts_with(prefix)) {
            result.push_back({cmd.name, cmd.description});
        }
    }
    return result;
}

} // namespace merak::commands
