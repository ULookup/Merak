#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace merak::worldbuilding {

enum class CreativePhase {
    Worldbuilding,
    CharacterCreation,
    PlotArchitecture,
    SceneWriting,
    Reflection
};

inline std::string to_string(CreativePhase p) {
    switch (p) {
        case CreativePhase::Worldbuilding:
            return "worldbuilding";
        case CreativePhase::CharacterCreation:
            return "character_creation";
        case CreativePhase::PlotArchitecture:
            return "plot_architecture";
        case CreativePhase::SceneWriting:
            return "scene_writing";
        case CreativePhase::Reflection:
            return "reflection";
    }
    return "unknown";
}

inline std::optional<CreativePhase> creative_phase_from_string(const std::string& s) {
    if (s == "worldbuilding") return CreativePhase::Worldbuilding;
    if (s == "character_creation") return CreativePhase::CharacterCreation;
    if (s == "plot_architecture") return CreativePhase::PlotArchitecture;
    if (s == "scene_writing") return CreativePhase::SceneWriting;
    if (s == "reflection") return CreativePhase::Reflection;
    return std::nullopt;
}

struct PipelineState {
    std::string world_id;
    CreativePhase current_phase = CreativePhase::Worldbuilding;
    std::optional<std::string> active_arc_id;
    std::optional<std::string> active_chapter_id;
    std::optional<std::string> active_scene_id;
    int scene_count_in_chapter = 0;
    int total_scenes_target = 0;
    bool needs_diary_update = false;
    bool needs_character_update = false;
    std::string last_updated;

    // ═══ New fields (PipelineManager engine) ═══
    std::string active_workflow;
    int chapter_count = 0;
    int total_chapters_target = 0;
    bool is_cycle_complete = false;
    int cycle_count = 0;
    nlohmann::json extra;
};

// Phase transition rules
std::vector<CreativePhase> allowed_next_phases(CreativePhase current);

// Generate phase guidance context for injection into system prompt
std::string generate_phase_context(const PipelineState& state);

// JSON serialization for PostgreSQL JSONB persistence
void to_json(nlohmann::json& j, const PipelineState& s);
void from_json(const nlohmann::json& j, PipelineState& s);

} // namespace merak::worldbuilding
