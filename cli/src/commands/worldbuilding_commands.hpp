#pragma once

#include <nlohmann/json.hpp>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace merak::commands {

enum class WorldbuildingAction {
    None,

    // World
    WorldList,
    WorldCreate,
    WorldUse,
    WorldDelete,
    WorldRename,

    // Agent
    AgentList,
    AgentCreate,
    AgentEdit,
    AgentHistory,
    AgentDelete,
    AgentRoute,

    // Story / Chapter / Arc / Scene
    StoryOverview,
    ChapterNew,
    ChapterUse,
    ChapterList,
    ChapterCurve,
    ArcNew,
    ArcList,
    SceneNew,
    SceneList,
    SceneUse,
    SceneEnd,
    SceneJump,

    // Time
    TimeNow,
    TimeAdvance,
    TimeCalendar,

    // Foreshadowing
    ForeshadowList,
    ForeshadowPlant,
    ForeshadowPay,
    ForeshadowAbandon,
    ForeshadowCheck,
    ForeshadowStats,

    // Secret
    SecretList,
    SecretAt,
    SecretCreate,
    SecretExpose,
    SecretCheck,

    // Voice
    VoiceCheck,
    VoiceAt,
    VoiceGroup,
    VoiceCompare,

    // Memory / Diary
    MemoryLatest,
    MemorySearch,
    DiaryShow,
};

struct WorldbuildingCommand {
    WorldbuildingAction action = WorldbuildingAction::None;
    std::vector<std::string> args;
    std::string current_world_id;
    std::string current_chapter_id;
    std::string current_scene_id;
};

// Parse a slash command into a structured action.
// Returns nullopt if the input does not match any worldbuilding command.
std::optional<WorldbuildingCommand>
parse_worldbuilding_command(const std::string& input,
                            const std::string& current_world_id,
                            const std::string& current_chapter_id,
                            const std::string& current_scene_id);

// Execute a parsed command via a callback that receives (method, path, body).
// Returns the JSON result to display to the user.
using HttpCallback = std::function<nlohmann::json(
    const std::string& method, const std::string& path,
    const nlohmann::json& body)>;

std::string execute_worldbuilding_command(const WorldbuildingCommand& cmd,
                                          const HttpCallback& http);

// Generate help text for all worldbuilding commands
std::string worldbuilding_help_text();

} // namespace merak::commands
