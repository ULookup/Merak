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
    std::vector<std::string> insert_flashback_scene(const std::string& world_id,
                                                    Scene scene);
    ChapterContext chapter_context(const std::string& world_id,
                                   const std::string& chapter_id) const;
    std::optional<Scene> get_scene(const std::string& world_id,
                                    const std::string& scene_id) const;

private:
    void initialize();

    WorldStore& worlds_;
    std::filesystem::path data_root_;
    std::unique_ptr<PgPool> pool_;
};

} // namespace merak::worldbuilding
