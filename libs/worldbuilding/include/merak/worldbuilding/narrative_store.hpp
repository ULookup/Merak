#pragma once

#include <merak/worldbuilding/world_models.hpp>
#include <merak/worldbuilding/world_store.hpp>
#include <merak/worldbuilding/pg_helpers.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace merak::worldbuilding {

struct ChapterContext {
    std::string chapter_pitch;
    std::vector<std::string> previous_scene_summaries;
    nlohmann::json emotional_curve_position;
    std::optional<std::string> arc_purpose;
    std::vector<std::string> open_foreshadowing_ids;
};

struct ArcSummary {
    std::string id, title, purpose, status, updated_at;
};

struct ChapterSummary {
    std::string id, title, status, updated_at;
    int number = 0;
    std::optional<std::string> arc_id;
    int scene_count = 0;
};

struct SceneSummary {
    std::string id, title, chapter_id, world_time, status, updated_at;
    std::vector<std::string> participant_ids;
};

struct CharacterAppearances {
    nlohmann::json chapters; // [{id, title, scene_count}]
    nlohmann::json scenes;   // [{id, title, chapter_id, status}]
};

class NarrativeStore {
public:
    NarrativeStore(WorldStore& worlds, std::string_view pg_conninfo,
                   std::filesystem::path data_root);

    StoryStructure create_story_structure(const std::string& world_id,
                                          NarrativeTemplate type);
    Arc create_arc(const std::string& world_id, Arc arc);
    Chapter create_chapter(const std::string& world_id, Chapter chapter);
    Section create_section(const std::string& world_id, Section section);
    Scene create_scene(const std::string& world_id, Scene scene);
    Scene update_scene_status(const std::string& world_id,
                              const std::string& scene_id,
                              SceneStatus status);
    Scene append_scene_text(const std::string& world_id,
                            const std::string& scene_id,
                            std::string markdown);
    TimelineEvent record_timeline_event(const std::string& world_id,
                                        TimelineEvent event);
    TimelineEvent advance_time(const std::string& world_id,
                               TimelineEvent event);
    std::vector<TimelineEvent> list_timeline_events(const std::string& world_id) const;
    std::optional<TimelineEvent> get_timeline_event(const std::string& world_id,
                                                     const std::string& event_id) const;
    std::vector<std::string> insert_flashback_scene(const std::string& world_id,
                                                    Scene scene);

    // 部分更新场景字段
    bool patch_scene(const std::string& world_id, const std::string& scene_id,
                     const nlohmann::json& fields);

    // 部分更新章节字段
    bool patch_chapter(const std::string& world_id, const std::string& chapter_id,
                       const nlohmann::json& fields);

    ChapterContext chapter_context(const std::string& world_id,
                                   const std::string& chapter_id) const;
    std::optional<Chapter> get_chapter(const std::string& world_id,
                                       const std::string& chapter_id) const;
    std::optional<Scene> get_scene(const std::string& world_id,
                                    const std::string& scene_id) const;
    std::vector<ArcSummary> list_arcs(const std::string& world_id) const;
    std::vector<ChapterSummary>
    list_chapters(const std::string& world_id,
                  std::optional<ChapterStatus> status = std::nullopt) const;
    std::vector<SceneSummary>
    list_scenes(const std::string& world_id,
                const std::optional<std::string>& chapter_id = std::nullopt,
                std::optional<SceneStatus> status = std::nullopt) const;

    bool reorder_chapters(const std::string& world_id, const nlohmann::json& order);
    CharacterAppearances find_character_appearances(const std::string& world_id,
                                                     const std::string& agent_id) const;

    bool delete_chapter(const std::string& world_id, const std::string& chapter_id);
    bool delete_scene(const std::string& world_id, const std::string& scene_id);

private:
    void initialize();

    WorldStore& worlds_;
    std::filesystem::path data_root_;
    std::unique_ptr<PgPool> pool_;
};

} // namespace merak::worldbuilding
