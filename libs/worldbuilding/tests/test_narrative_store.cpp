#include <gtest/gtest.h>
#include <merak/worldbuilding/ids.hpp>
#include <merak/worldbuilding/narrative_store.hpp>
#include <merak/worldbuilding/world_store.hpp>

#include "test_helpers.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace merak::worldbuilding;

namespace {

std::filesystem::path temp_dir() {
    auto path =
        std::filesystem::temp_directory_path() / make_id("narrative_test");
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

std::string slurp(const std::filesystem::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

} // namespace

TEST(NarrativeStore, CreateStoryStructurePersistsThreeActStages) {
    auto root = temp_dir();
    WorldStore worlds(test_pg_conninfo(), root);
    auto world = worlds.create_world("北境", "雪原史诗");
    NarrativeStore narrative(worlds, test_pg_conninfo(), root);

    auto structure =
        narrative.create_story_structure(world.id, NarrativeTemplate::ThreeAct);

    EXPECT_EQ(structure.template_type, NarrativeTemplate::ThreeAct);
    EXPECT_EQ(structure.name, "three_act");
    EXPECT_EQ(structure.stages, std::vector<std::string>({"建立", "对抗", "解决"}));

    const auto structure_path =
        worlds.world_path(world.id) / "story_structure.json";
    ASSERT_TRUE(std::filesystem::exists(structure_path));
    auto json = nlohmann::json::parse(slurp(structure_path));
    EXPECT_EQ(json["template_type"], "three_act");
    EXPECT_EQ(json["stages"][0], "建立");
    EXPECT_EQ(json["stages"][1], "对抗");
    EXPECT_EQ(json["stages"][2], "解决");
}

TEST(NarrativeStore, CreateArcChapterSectionScenePreservesHierarchy) {
    auto root = temp_dir();
    WorldStore worlds(test_pg_conninfo(), root);
    auto world = worlds.create_world("北境", "雪原史诗");
    NarrativeStore narrative(worlds, test_pg_conninfo(), root);

    Arc arc;
    arc.title = "守住狼烟";
    arc.purpose = "让主角接受守护者身份";
    auto created_arc = narrative.create_arc(world.id, arc);

    Chapter chapter;
    chapter.title = "雪夜来信";
    chapter.number = 1;
    chapter.pitch = "旧友求援打破边境平静";
    chapter.arc_id = created_arc.id;
    auto created_chapter = narrative.create_chapter(world.id, chapter);

    Section section;
    section.chapter_id = created_chapter.id;
    section.title = "城门";
    section.order = 1;
    auto created_section = narrative.create_section(world.id, section);

    Scene scene;
    scene.title = "信使倒在门前";
    scene.chapter_id = created_chapter.id;
    scene.section_id = created_section.id;
    scene.world_time = "第三日夜";
    scene.participant_ids = {"agent_linshi", "agent_luzheng"};
    scene.narrative = "信使带来一枚烧焦的狼烟令。";
    auto created_scene = narrative.create_scene(world.id, scene);

    auto chapter_json = nlohmann::json::parse(
        slurp(worlds.world_path(world.id) / "chapters" /
              (created_chapter.id + ".json")));
    auto section_json = nlohmann::json::parse(
        slurp(worlds.world_path(world.id) / "chapters" / created_chapter.id /
              "sections" / (created_section.id + ".json")));
    auto scene_json = nlohmann::json::parse(
        slurp(worlds.world_path(world.id) / "scenes" /
              (created_scene.id + ".json")));

    EXPECT_EQ(chapter_json["arc_id"], created_arc.id);
    ASSERT_EQ(chapter_json["scene_ids"].size(), 1);
    EXPECT_EQ(chapter_json["scene_ids"][0], created_scene.id);
    ASSERT_EQ(section_json["scene_ids"].size(), 1);
    EXPECT_EQ(section_json["scene_ids"][0], created_scene.id);
    EXPECT_EQ(scene_json["chapter_id"], created_chapter.id);
    EXPECT_EQ(scene_json["section_id"], created_section.id);
}

TEST(NarrativeStore, ChapterToScenePathWorksWithoutArcOrSection) {
    auto root = temp_dir();
    WorldStore worlds(test_pg_conninfo(), root);
    auto world = worlds.create_world("北境", "雪原史诗");
    NarrativeStore narrative(worlds, test_pg_conninfo(), root);

    Chapter chapter;
    chapter.title = "无弧线章节";
    chapter.number = 1;
    chapter.pitch = "直接从章节进入场景";
    auto created_chapter = narrative.create_chapter(world.id, chapter);

    Scene scene;
    scene.title = "独立场景";
    scene.chapter_id = created_chapter.id;
    scene.world_time = "第一日晨";
    scene.participant_ids = {"agent_linshi"};
    auto created_scene = narrative.create_scene(world.id, scene);

    EXPECT_EQ(created_scene.chapter_id, created_chapter.id);
    EXPECT_FALSE(created_scene.section_id.has_value());
}

TEST(NarrativeStore, SceneRequiresChapterWorldTimeAndParticipants) {
    auto root = temp_dir();
    WorldStore worlds(test_pg_conninfo(), root);
    auto world = worlds.create_world("北境", "雪原史诗");
    NarrativeStore narrative(worlds, test_pg_conninfo(), root);

    Scene missing_chapter;
    missing_chapter.world_time = "第一日晨";
    missing_chapter.participant_ids = {"agent_linshi"};
    EXPECT_THROW(narrative.create_scene(world.id, missing_chapter),
                 std::runtime_error);

    Chapter chapter;
    chapter.title = "验证章节";
    chapter.number = 1;
    auto created_chapter = narrative.create_chapter(world.id, chapter);

    Scene missing_time;
    missing_time.chapter_id = created_chapter.id;
    missing_time.participant_ids = {"agent_linshi"};
    EXPECT_THROW(narrative.create_scene(world.id, missing_time),
                 std::runtime_error);

    Scene missing_participants;
    missing_participants.chapter_id = created_chapter.id;
    missing_participants.world_time = "第一日晨";
    EXPECT_THROW(narrative.create_scene(world.id, missing_participants),
                 std::runtime_error);
}

TEST(NarrativeStore, AdvanceTimeAppendsTimelineEvent) {
    auto root = temp_dir();
    WorldStore worlds(test_pg_conninfo(), root);
    auto world = worlds.create_world("北境", "雪原史诗");
    NarrativeStore narrative(worlds, test_pg_conninfo(), root);

    TimelineEvent event;
    event.world_time = "第四日午";
    event.description = "狼烟塔重新点燃";
    event.recorded_by = "agent_god";
    event.affected_character_ids = {"agent_linshi"};
    event.related_scene_ids = {"scene_gate"};
    auto recorded = narrative.advance_time(world.id, event);

    EXPECT_FALSE(recorded.id.empty());
    auto timeline = nlohmann::json::parse(
        slurp(worlds.world_path(world.id) / "timeline.json"));
    ASSERT_EQ(timeline["events"].size(), 1);
    EXPECT_EQ(timeline["events"][0]["id"], recorded.id);
    EXPECT_EQ(timeline["events"][0]["world_time"], "第四日午");
    EXPECT_EQ(timeline["events"][0]["description"], "狼烟塔重新点燃");
}

TEST(NarrativeStore, InsertFlashbackMarksSceneAndWarnsOnParticipantConflicts) {
    auto root = temp_dir();
    WorldStore worlds(test_pg_conninfo(), root);
    auto world = worlds.create_world("北境", "雪原史诗");
    NarrativeStore narrative(worlds, test_pg_conninfo(), root);

    Chapter chapter;
    chapter.title = "交错时间";
    chapter.number = 1;
    auto created_chapter = narrative.create_chapter(world.id, chapter);

    Scene later;
    later.title = "后来相遇";
    later.chapter_id = created_chapter.id;
    later.world_time = "第三日夜";
    later.participant_ids = {"agent_linshi", "agent_luzheng"};
    auto later_scene = narrative.create_scene(world.id, later);

    Scene flashback;
    flashback.title = "旧日误会";
    flashback.chapter_id = created_chapter.id;
    flashback.world_time = "第一日晨";
    flashback.participant_ids = {"agent_linshi"};
    auto warnings = narrative.insert_flashback_scene(world.id, flashback);

    EXPECT_EQ(warnings.size(), 1);
    EXPECT_NE(warnings[0].find(later_scene.id), std::string::npos);

    auto chapter_json = nlohmann::json::parse(
        slurp(worlds.world_path(world.id) / "chapters" /
              (created_chapter.id + ".json")));
    ASSERT_EQ(chapter_json["scene_ids"].size(), 2);
    const auto flashback_id = chapter_json["scene_ids"][1].get<std::string>();
    auto scene_json = nlohmann::json::parse(
        slurp(worlds.world_path(world.id) / "scenes" /
              (flashback_id + ".json")));
    EXPECT_EQ(scene_json["is_flashback"], true);
}

TEST(NarrativeStore, ChapterContextAssemblesStoredNarrativeSignals) {
    auto root = temp_dir();
    WorldStore worlds(test_pg_conninfo(), root);
    auto world = worlds.create_world("北境", "雪原史诗");
    NarrativeStore narrative(worlds, test_pg_conninfo(), root);

    Arc arc;
    arc.title = "守住狼烟";
    arc.purpose = "让主角接受守护者身份";
    auto created_arc = narrative.create_arc(world.id, arc);

    Chapter previous;
    previous.title = "旧塔";
    previous.number = 1;
    auto previous_chapter = narrative.create_chapter(world.id, previous);
    Scene previous_scene;
    previous_scene.title = "塔下争执";
    previous_scene.chapter_id = previous_chapter.id;
    previous_scene.world_time = "第一日夜";
    previous_scene.participant_ids = {"agent_linshi"};
    previous_scene.narrative = "林霜拒绝离开狼烟塔。\n她还没有说出原因。";
    auto created_previous_scene =
        narrative.create_scene(world.id, previous_scene);
    narrative.update_scene_status(world.id, created_previous_scene.id,
                                  SceneStatus::Completed);

    Chapter current;
    current.title = "雪夜来信";
    current.number = 2;
    current.pitch = "旧友求援打破边境平静";
    current.arc_id = created_arc.id;
    current.emotional_curve = nlohmann::json::object(
        {{"beat", "pressure"}, {"intensity", 0.7}});
    current.foreshadowing_planted = {"shadow_old_oath", "shadow_burned_token"};
    current.foreshadowing_paid = {"shadow_old_oath"};
    auto current_chapter = narrative.create_chapter(world.id, current);

    auto context = narrative.chapter_context(world.id, current_chapter.id);

    EXPECT_EQ(context.chapter_pitch, "旧友求援打破边境平静");
    ASSERT_EQ(context.previous_scene_summaries.size(), 1);
    EXPECT_EQ(context.previous_scene_summaries[0], "林霜拒绝离开狼烟塔。");
    EXPECT_EQ(context.emotional_curve_position, current.emotional_curve);
    ASSERT_TRUE(context.arc_purpose.has_value());
    EXPECT_EQ(*context.arc_purpose, "让主角接受守护者身份");
    EXPECT_EQ(context.open_foreshadowing_ids,
              std::vector<std::string>({"shadow_burned_token"}));
}
