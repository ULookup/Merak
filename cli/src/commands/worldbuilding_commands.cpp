#include "worldbuilding_commands.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace merak::commands {

namespace {

std::vector<std::string> split(const std::string& input) {
    std::vector<std::string> tokens;
    std::istringstream in(input);
    std::string token;
    while (in >> token) tokens.push_back(token);
    return tokens;
}

std::string join(const std::vector<std::string>& tokens, size_t start, const std::string& sep) {
    std::string result;
    for (size_t i = start; i < tokens.size(); ++i) {
        if (i > start) result += sep;
        result += tokens[i];
    }
    return result;
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

bool requires_world(WorldbuildingAction action) {
    switch (action) {
    case WorldbuildingAction::WorldList:
    case WorldbuildingAction::WorldCreate:
    case WorldbuildingAction::WorldUse:
    case WorldbuildingAction::WorldDelete:
    case WorldbuildingAction::None:
        return false;
    default:
        return true;
    }
}

} // namespace

std::optional<WorldbuildingCommand>
parse_worldbuilding_command(const std::string& input,
                            const std::string& current_world_id,
                            const std::string& current_chapter_id,
                            const std::string& current_scene_id) {
    auto tokens = split(input);
    if (tokens.empty()) return std::nullopt;

    WorldbuildingCommand cmd;
    cmd.current_world_id = current_world_id;
    cmd.current_chapter_id = current_chapter_id;
    cmd.current_scene_id = current_scene_id;

    const auto& cmd_name = tokens[0];

    // @agent routing
    if (starts_with(cmd_name, "@")) {
        if (cmd_name == "@clear") {
            cmd.action = WorldbuildingAction::AgentRoute;
            cmd.args = {"clear"};
            return cmd;
        }
        cmd.action = WorldbuildingAction::AgentRoute;
        cmd.args = {cmd_name.substr(1)};
        cmd.args.push_back(join(tokens, 1, " "));
        return cmd;
    }

    // ── World ──
    if (cmd_name == "/world") {
        if (tokens.size() >= 2) {
            if (tokens[1] == "list") cmd.action = WorldbuildingAction::WorldList;
            else if (tokens[1] == "create") {
                cmd.action = WorldbuildingAction::WorldCreate;
                cmd.args = {join(tokens, 2, " ")};
            } else if (tokens[1] == "use") {
                cmd.action = WorldbuildingAction::WorldUse;
                if (tokens.size() >= 3) cmd.args = {tokens[2]};
            } else if (tokens[1] == "delete") {
                cmd.action = WorldbuildingAction::WorldDelete;
                if (tokens.size() >= 3) cmd.args = {tokens[2]};
            }
        } else {
            cmd.action = WorldbuildingAction::WorldList;
        }
        return cmd;
    }

    // ── Agent ──
    if (cmd_name == "/agent") {
        if (tokens.size() >= 2) {
            if (tokens[1] == "list") cmd.action = WorldbuildingAction::AgentList;
            else if (tokens[1] == "create") {
                cmd.action = WorldbuildingAction::AgentCreate;
                cmd.args = {tokens.begin() + 2, tokens.end()};
            } else if (tokens[1] == "edit") {
                cmd.action = WorldbuildingAction::AgentEdit;
                if (tokens.size() >= 3) cmd.args = {tokens[2]};
            } else if (tokens[1] == "history") {
                cmd.action = WorldbuildingAction::AgentHistory;
                if (tokens.size() >= 3) cmd.args = {tokens[2]};
            } else if (tokens[1] == "delete") {
                cmd.action = WorldbuildingAction::AgentDelete;
                if (tokens.size() >= 3) cmd.args = {tokens[2]};
            }
        } else {
            cmd.action = WorldbuildingAction::AgentList;
        }
        return cmd;
    }

    // ── Story ──
    if (cmd_name == "/story") {
        cmd.action = WorldbuildingAction::StoryOverview;
        return cmd;
    }

    // ── Chapter ──
    if (cmd_name == "/chapter") {
        if (tokens.size() >= 2) {
            if (tokens[1] == "new") {
                cmd.action = WorldbuildingAction::ChapterNew;
                cmd.args = {join(tokens, 2, " ")};
            } else if (tokens[1] == "use") {
                cmd.action = WorldbuildingAction::ChapterUse;
                if (tokens.size() >= 3) cmd.args = {tokens[2]};
            } else if (tokens[1] == "list") {
                cmd.action = WorldbuildingAction::ChapterList;
            } else if (tokens[1] == "curve") {
                cmd.action = WorldbuildingAction::ChapterCurve;
            }
        } else {
            cmd.action = WorldbuildingAction::ChapterList;
        }
        return cmd;
    }

    // ── Arc ──
    if (cmd_name == "/arc") {
        if (tokens.size() >= 2) {
            if (tokens[1] == "new") {
                cmd.action = WorldbuildingAction::ArcNew;
                cmd.args = {join(tokens, 2, " ")};
            } else if (tokens[1] == "list") {
                cmd.action = WorldbuildingAction::ArcList;
            }
        } else {
            cmd.action = WorldbuildingAction::ArcList;
        }
        return cmd;
    }

    // ── Scene ──
    if (cmd_name == "/scene") {
        if (tokens.size() >= 2) {
            if (tokens[1] == "new") {
                cmd.action = WorldbuildingAction::SceneNew;
                cmd.args = {join(tokens, 2, " ")};
            } else if (tokens[1] == "list") {
                cmd.action = WorldbuildingAction::SceneList;
            } else if (tokens[1] == "use") {
                cmd.action = WorldbuildingAction::SceneUse;
                if (tokens.size() >= 3) cmd.args = {tokens[2]};
            } else if (tokens[1] == "end") {
                cmd.action = WorldbuildingAction::SceneEnd;
            } else if (tokens[1] == "jump") {
                cmd.action = WorldbuildingAction::SceneJump;
                if (tokens.size() >= 3) cmd.args = {tokens[2]};
            }
        } else {
            cmd.action = WorldbuildingAction::SceneList;
        }
        return cmd;
    }

    // ── Time ──
    if (cmd_name == "/time") {
        if (tokens.size() >= 2) {
            if (tokens[1] == "now") cmd.action = WorldbuildingAction::TimeNow;
            else if (tokens[1] == "advance") {
                cmd.action = WorldbuildingAction::TimeAdvance;
                if (tokens.size() >= 3) cmd.args = {tokens[2]};
            } else if (tokens[1] == "calendar") cmd.action = WorldbuildingAction::TimeCalendar;
        } else {
            cmd.action = WorldbuildingAction::TimeNow;
        }
        return cmd;
    }

    // ── Foreshadow ──
    if (cmd_name == "/foreshadow") {
        if (tokens.size() >= 2) {
            if (tokens[1] == "list") cmd.action = WorldbuildingAction::ForeshadowList;
            else if (tokens[1] == "plant") {
                cmd.action = WorldbuildingAction::ForeshadowPlant;
                cmd.args = {join(tokens, 2, " ")};
            } else if (tokens[1] == "pay") {
                cmd.action = WorldbuildingAction::ForeshadowPay;
                if (tokens.size() >= 3) cmd.args = {tokens[2]};
            } else if (tokens[1] == "abandon") {
                cmd.action = WorldbuildingAction::ForeshadowAbandon;
                if (tokens.size() >= 3) cmd.args = {tokens[2]};
            } else if (tokens[1] == "check") cmd.action = WorldbuildingAction::ForeshadowCheck;
            else if (tokens[1] == "stats") cmd.action = WorldbuildingAction::ForeshadowStats;
        } else {
            cmd.action = WorldbuildingAction::ForeshadowList;
        }
        return cmd;
    }

    // ── Secret ──
    if (cmd_name == "/secret") {
        if (tokens.size() >= 2) {
            if (tokens[1] == "list") cmd.action = WorldbuildingAction::SecretList;
            else if (tokens[1] == "create") {
                cmd.action = WorldbuildingAction::SecretCreate;
                cmd.args = {join(tokens, 2, " ")};
            } else if (tokens[1] == "expose") {
                cmd.action = WorldbuildingAction::SecretExpose;
                if (tokens.size() >= 3) cmd.args = {tokens[2]};
            } else if (tokens[1] == "check") {
                cmd.action = WorldbuildingAction::SecretCheck;
                cmd.args = {tokens.begin() + 2, tokens.end()};
            } else if (starts_with(tokens[1], "@")) {
                cmd.action = WorldbuildingAction::SecretAt;
                cmd.args = {tokens[1].substr(1)};
            }
        } else {
            cmd.action = WorldbuildingAction::SecretList;
        }
        return cmd;
    }

    // ── Voice ──
    if (cmd_name == "/voice") {
        if (tokens.size() >= 2) {
            if (tokens[1] == "check") cmd.action = WorldbuildingAction::VoiceCheck;
            else if (tokens[1] == "group") cmd.action = WorldbuildingAction::VoiceGroup;
            else if (tokens[1] == "compare") {
                cmd.action = WorldbuildingAction::VoiceCompare;
                cmd.args = {tokens.begin() + 2, tokens.end()};
            } else if (starts_with(tokens[1], "@")) {
                cmd.action = WorldbuildingAction::VoiceAt;
                cmd.args = {tokens[1].substr(1)};
            }
        } else {
            cmd.action = WorldbuildingAction::VoiceCheck;
        }
        return cmd;
    }

    // ── Memory ──
    if (cmd_name == "/memory") {
        if (tokens.size() >= 2) {
            if (starts_with(tokens[1], "@")) {
                if (tokens.size() >= 3 && tokens[2] == "latest") {
                    cmd.action = WorldbuildingAction::MemoryLatest;
                    cmd.args = {tokens[1].substr(1)};
                } else if (tokens.size() >= 3 && tokens[2] == "search") {
                    cmd.action = WorldbuildingAction::MemorySearch;
                    cmd.args = {tokens[1].substr(1), join(tokens, 3, " ")};
                }
            }
        }
        return cmd;
    }

    // ── Diary ──
    if (cmd_name == "/diary") {
        if (tokens.size() >= 2 && starts_with(tokens[1], "@")) {
            if (tokens.size() >= 3 && tokens[2] == "show") {
                cmd.action = WorldbuildingAction::DiaryShow;
                cmd.args = {tokens[1].substr(1)};
            }
        }
        return cmd;
    }

    return std::nullopt;
}

std::string execute_worldbuilding_command(const WorldbuildingCommand& cmd,
                                          const HttpCallback& http) {
    auto method = [&](const std::string& path) -> std::string {
        try {
            return http("GET", path, {}).dump(2);
        } catch (const std::exception& e) {
            return std::string("Error: ") + e.what();
        }
    };

    auto post = [&](const std::string& path, const nlohmann::json& body) -> std::string {
        try {
            return http("POST", path, body).dump(2);
        } catch (const std::exception& e) {
            return std::string("Error: ") + e.what();
        }
    };

    auto del = [&](const std::string& path) -> std::string {
        try {
            return http("DELETE", path, {}).dump(2);
        } catch (const std::exception& e) {
            return std::string("Error: ") + e.what();
        }
    };

    std::string wid = cmd.current_world_id;
    if (requires_world(cmd.action) && wid.empty()) {
        return "Error: select a world first with /world use <id>";
    }

    switch (cmd.action) {
    case WorldbuildingAction::WorldList:
        return method("/api/worldbuilding/worlds");
    case WorldbuildingAction::WorldCreate:
        return post("/api/worldbuilding/worlds", {{"name", cmd.args.empty() ? "" : cmd.args[0]}});
    case WorldbuildingAction::WorldUse:
        return "Switched to world: " + (cmd.args.empty() ? "?" : cmd.args[0]);
    case WorldbuildingAction::WorldDelete:
        return del("/api/worldbuilding/worlds/" + (cmd.args.empty() ? "" : cmd.args[0]));
    case WorldbuildingAction::AgentList:
        return method("/api/worldbuilding/" + wid + "/agents");
    case WorldbuildingAction::AgentCreate:
        return post("/api/worldbuilding/" + wid + "/agents", {{"args", cmd.args}});
    case WorldbuildingAction::SceneNew:
        return post("/api/worldbuilding/" + wid + "/scenes", {
            {"title", cmd.args.empty() ? "" : cmd.args[0]},
            {"chapter_id", cmd.current_chapter_id}
        });
    case WorldbuildingAction::SceneEnd:
        return post("/api/worldbuilding/" + wid + "/scenes/" + cmd.current_scene_id + "/end", {});
    case WorldbuildingAction::ForeshadowList:
        return method("/api/worldbuilding/" + wid + "/foreshadowing");
    case WorldbuildingAction::ForeshadowPlant:
        return post("/api/worldbuilding/" + wid + "/foreshadowing", {
            {"content", cmd.args.empty() ? "" : cmd.args[0]}
        });
    case WorldbuildingAction::ForeshadowPay:
        return post("/api/worldbuilding/" + wid + "/foreshadowing/" + (cmd.args.empty() ? "" : cmd.args[0]) + "/pay", {});
    case WorldbuildingAction::ForeshadowAbandon:
        return post("/api/worldbuilding/" + wid + "/foreshadowing/" + (cmd.args.empty() ? "" : cmd.args[0]) + "/abandon", {});
    case WorldbuildingAction::ForeshadowCheck:
        return method("/api/worldbuilding/" + wid + "/foreshadowing?status=open");
    case WorldbuildingAction::ForeshadowStats:
        return method("/api/worldbuilding/" + wid + "/foreshadowing/stats");
    case WorldbuildingAction::SecretList:
        return method("/api/worldbuilding/" + wid + "/secrets");
    case WorldbuildingAction::SecretCreate:
        return post("/api/worldbuilding/" + wid + "/secrets", {
            {"description", cmd.args.empty() ? "" : cmd.args[0]}
        });
    case WorldbuildingAction::SecretExpose:
        return post("/api/worldbuilding/" + wid + "/secrets/" + (cmd.args.empty() ? "" : cmd.args[0]) + "/expose", {});
    case WorldbuildingAction::VoiceCheck:
        return method("/api/worldbuilding/" + wid + "/voice/check");
    case WorldbuildingAction::VoiceGroup:
        return method("/api/worldbuilding/" + wid + "/voice/group");
    case WorldbuildingAction::VoiceCompare:
        return method("/api/worldbuilding/" + wid + "/voice/compare");
    case WorldbuildingAction::MemoryLatest:
        return method("/api/worldbuilding/" + wid + "/agents/" + (cmd.args.empty() ? "" : cmd.args[0]) + "/memory?limit=5");
    default:
        return "Command not yet connected to API: use service directly.";
    }
}

std::string worldbuilding_help_text() {
    return R"(Worldbuilding Commands:
  /world list|create <name>|use <id>|delete <id>
  /agent list|create character|manager|edit|history|delete
  @agent_name <message>     Route message to agent
  @clear                    Clear agent route
  /story overview
  /chapter new <title>|use <id>|list|curve
  /arc new <title>|list
  /scene new <title>|list|use <id>|end|jump <time>
  /time now|advance <delta>|calendar
  /foreshadow list|plant <content>|pay <id>|abandon <id>|check|stats
  /secret list|@name|create <desc>|expose <id>|check @A @B
  /voice check|@name|group|compare @A @B
  /memory @name latest|search <query>
  /diary @name show)";
}

} // namespace merak::commands
