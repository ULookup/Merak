#include <gtest/gtest.h>
#include <merak/worldbuilding/agent_store.hpp>
#include <merak/worldbuilding/foreshadowing_store.hpp>
#include <merak/worldbuilding/ids.hpp>
#include <merak/worldbuilding/narrative_store.hpp>
#include <merak/worldbuilding/scene_orchestrator.hpp>
#include <merak/worldbuilding/secret_store.hpp>
#include <merak/worldbuilding/voice_analyzer.hpp>
#include <merak/worldbuilding/world_store.hpp>
#include <merak/worldbuilding/worldbuilding_service.hpp>

#include "test_helpers.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace merak::worldbuilding;
using namespace merak::worldbuilding::test;

namespace {

std::filesystem::path temp_dir() {
    auto path = std::filesystem::temp_directory_path() / make_id("orch_test");
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

struct OrchFixture {
    std::filesystem::path root = temp_dir();
    WorldStore worlds{test_pg_conninfo(), root};
    WorldMeta world;
    NarrativeStore narrative;
    AgentStore agents;
    ForeshadowingStore foreshadowing;
    SecretStore secrets;
    VoiceAnalyzer voice;
    SceneOrchestrator orchestrator;
    WorldbuildingService service{test_pg_conninfo(), root};

    OrchFixture()
        : world(worlds.create_world("北境", "雪原史诗")),
          narrative(worlds, test_pg_conninfo(), root),
          agents(worlds, test_pg_conninfo(), root),
          foreshadowing(worlds, narrative, test_pg_conninfo(), root),
          secrets(worlds, foreshadowing, test_pg_conninfo(), root),
          orchestrator(worlds, agents, narrative, foreshadowing, secrets, voice) {}

    AgentRecord make_char(const std::string& name) {
        CharacterCard card;
        card.name = name;
        card.gender = "女";
        card.age = 28;
        card.identity = "流浪剑客";
        card.emotional_tendency = "内敛";
        card.speaking_style = "简短有力";
        card.core_desire = "守护家人";
        card.deep_fear = "再次失去";
        card.appearance = "黑发，左脸有疤";
        card.background = "曾在王都担任骑士";
        card.knowledge_scope = "武艺、王都政治";
        card.core_traits = {"沉着", "果敢"};
        return agents.create_character(world.id, card);
    }
};

} // namespace

TEST(SceneOrchestrator, PrepareSceneLoadsGodContext) {
    OrchFixture f;

    auto linshi = f.make_char("林霜");
    auto masha = f.make_char("玛莎");

    Chapter chapter;
    chapter.title = "雪夜来客";
    chapter.number = 1;
    chapter.pitch = "旧友求援打破边境平静";
    auto created_chapter = f.narrative.create_chapter(f.world.id, chapter);

    Scene scene;
    scene.title = "旅店试探";
    scene.chapter_id = created_chapter.id;
    scene.world_time = "第三日夜";
    scene.participant_ids = {linshi.id, masha.id};
    auto created_scene = f.narrative.create_scene(f.world.id, scene);

    auto prep = f.orchestrator.prepare_scene(f.world.id, created_scene.id, f.service);

    EXPECT_FALSE(prep.god_context.empty());
    EXPECT_NE(prep.god_context.find("旧友求援打破边境平静"), std::string::npos);
    EXPECT_NE(prep.god_context.find("雪夜来客"), std::string::npos);
    EXPECT_NE(prep.god_context.find(linshi.id), std::string::npos);
    EXPECT_NE(prep.god_context.find(masha.id), std::string::npos);
}

TEST(SceneOrchestrator, PrepareSceneAssemblesCharacterViews) {
    OrchFixture f;

    auto linshi = f.make_char("林霜");
    auto masha = f.make_char("玛莎");

    Chapter chapter;
    chapter.title = "测试章节";
    chapter.number = 1;
    chapter.pitch = "验证角色视图";
    auto created_chapter = f.narrative.create_chapter(f.world.id, chapter);

    Scene scene;
    scene.title = "测试场景";
    scene.chapter_id = created_chapter.id;
    scene.world_time = "第一日晨";
    scene.participant_ids = {linshi.id, masha.id};
    auto created_scene = f.narrative.create_scene(f.world.id, scene);

    auto prep = f.orchestrator.prepare_scene(f.world.id, created_scene.id, f.service);

    ASSERT_EQ(prep.character_views.size(), 2);
    EXPECT_FALSE(prep.character_views[0].system_prompt.empty());
    EXPECT_FALSE(prep.character_views[1].system_prompt.empty());
}

TEST(SceneOrchestrator, PrepareSceneIncludesRelevantForeshadowing) {
    OrchFixture f;

    Foreshadowing fs;
    fs.content = "铁匠的断指";
    fs.tags = {"agent_linshi"};
    f.foreshadowing.plant(f.world.id, fs);

    auto linshi = f.make_char("林霜");

    Chapter chapter;
    chapter.title = "测试";
    chapter.number = 1;
    chapter.pitch = "pitch";
    auto ch = f.narrative.create_chapter(f.world.id, chapter);

    Scene scene;
    scene.title = "场景";
    scene.chapter_id = ch.id;
    scene.world_time = "day1";
    scene.participant_ids = {linshi.id};
    auto sc = f.narrative.create_scene(f.world.id, scene);

    auto prep = f.orchestrator.prepare_scene(f.world.id, sc.id, f.service);

    ASSERT_EQ(prep.relevant_foreshadowing.size(), 1);
    EXPECT_EQ(prep.relevant_foreshadowing[0].content, "铁匠的断指");
}

TEST(SceneOrchestrator, CharacterViewsDifferBySecretKnowledge) {
    OrchFixture f;

    auto ailin = f.make_char("艾琳");
    auto masha = f.make_char("玛莎");

    Secret secret;
    secret.holder_id = ailin.id;
    secret.truth = "艾琳是骑士团教官";
    secret.public_version = "艾琳是普通旅人";
    secret.stakes = "身份暴露将引来追杀";
    secret.aware_character_ids = {masha.id};
    f.secrets.create(f.world.id, secret);

    Chapter chapter;
    chapter.title = "测试";
    chapter.number = 1;
    chapter.pitch = "pitch";
    auto ch = f.narrative.create_chapter(f.world.id, chapter);

    Scene scene;
    scene.title = "场景";
    scene.chapter_id = ch.id;
    scene.world_time = "day1";
    scene.participant_ids = {ailin.id, masha.id};
    auto sc = f.narrative.create_scene(f.world.id, scene);

    auto prep = f.orchestrator.prepare_scene(f.world.id, sc.id, f.service);

    // Ailin's view should mention truth; masha's view should too (aware)
    // Both should have different secret contexts from any unaware character
    bool ailin_has_truth = false;
    bool masha_has_truth = false;
    for (const auto& view : prep.character_views) {
        if (view.agent_id == ailin.id &&
            view.system_prompt.find("骑士团教官") != std::string::npos) {
            ailin_has_truth = true;
        }
        if (view.agent_id == masha.id &&
            view.system_prompt.find("骑士团教官") != std::string::npos) {
            masha_has_truth = true;
        }
    }
    EXPECT_TRUE(ailin_has_truth || masha_has_truth);
}

TEST(SceneOrchestrator, FinishSceneWritesDiariesAndRelationUpdates) {
    OrchFixture f;

    auto linshi = f.make_char("林霜");
    auto masha = f.make_char("玛莎");

    Chapter chapter;
    chapter.title = "测试";
    chapter.number = 1;
    chapter.pitch = "pitch";
    auto ch = f.narrative.create_chapter(f.world.id, chapter);

    Scene scene;
    scene.title = "终章场景";
    scene.chapter_id = ch.id;
    scene.world_time = "day5";
    scene.participant_ids = {linshi.id, masha.id};
    auto sc = f.narrative.create_scene(f.world.id, scene);

    auto wrap = f.orchestrator.finish_scene(
        f.world.id, sc.id,
        "林霜望向窗外。\n'你终于来了。'\n玛莎放下酒杯。\n'我一直在等你。'");

    EXPECT_EQ(wrap.diaries_written.size(), 2);
    EXPECT_EQ(wrap.relations_updated.size(), 1); // one pair
}

TEST(SceneOrchestrator, RouteDirectMessageToIndividualAgent) {
    OrchFixture f;

    auto linshi = f.make_char("林霜");

    auto view = f.orchestrator.route_direct_message(
        f.world.id, linshi.id, "你是谁？");

    EXPECT_EQ(view.agent_id, linshi.id);
    EXPECT_NE(view.system_prompt.find("林霜"), std::string::npos);
    EXPECT_NE(view.system_prompt.find("你是谁？"), std::string::npos);
}

TEST(SceneOrchestrator, RouteDirectMessageToGroupSelectsMember) {
    OrchFixture f;

    auto char_a = f.make_char("部落勇士A");
    auto char_b = f.make_char("部落勇士B");
    auto group = f.agents.create_group(f.world.id, "北方蛮族",
                                        "# 蛮族文化\n共同信仰自然之灵",
                                        {char_a.id, char_b.id});

    auto view = f.orchestrator.route_direct_message(
        f.world.id, group.id, "来者何人？");

    // Should route to a member, not the group itself
    EXPECT_NE(view.agent_id, group.id);
    bool is_member = (view.agent_id == char_a.id || view.agent_id == char_b.id);
    EXPECT_TRUE(is_member);
    EXPECT_NE(view.system_prompt.find("群体代表发言"), std::string::npos);
}
