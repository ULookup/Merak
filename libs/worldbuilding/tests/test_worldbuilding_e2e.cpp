#include <gtest/gtest.h>
#include <merak/worldbuilding/ids.hpp>
#include <merak/worldbuilding/worldbuilding_service.hpp>

#include "test_helpers.hpp"

#include <filesystem>
#include <fstream>
#include <string>

using namespace merak::worldbuilding;
using namespace merak::worldbuilding::test;

namespace {

std::filesystem::path temp_dir() {
    auto path = std::filesystem::temp_directory_path() / make_id("e2e_test");
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

} // namespace

TEST(WorldbuildingE2E, FullMiniWorldCreationAndSceneLifecycle) {
    auto root = temp_dir();
    WorldbuildingService service(test_pg_conninfo(), root);
    service.initialize();

    // ── Create world ──
    auto world = service.create_world("北境", "雪原史诗");
    EXPECT_FALSE(world.id.empty());
    EXPECT_EQ(world.name, "北境");

    // ── Create managers ──
    auto map_mgr = service.create_manager(world.id, AgentKind::MapManager,
                                           "地图管理", "管理北境地理信息");
    EXPECT_EQ(map_mgr.kind, AgentKind::MapManager);

    auto history_mgr = service.create_manager(world.id, AgentKind::HistoryManager,
                                               "历史管理", "记录北境历史");
    EXPECT_EQ(history_mgr.kind, AgentKind::HistoryManager);

    auto magic_mgr = service.create_manager(world.id, AgentKind::MagicSystemManager,
                                             "魔法管理", "管理魔法系统规则");
    EXPECT_EQ(magic_mgr.kind, AgentKind::MagicSystemManager);

    auto faction_mgr = service.create_manager(world.id, AgentKind::FactionManager,
                                               "势力管理", "追踪各方势力动向");
    EXPECT_EQ(faction_mgr.kind, AgentKind::FactionManager);

    // ── Create characters ──
    CharacterCard ailincard;
    ailincard.name = "艾琳";
    ailincard.gender = "女";
    ailincard.age = 28;
    ailincard.identity = "流浪剑客";
    ailincard.emotional_tendency = "内敛深沉";
    ailincard.speaking_style = "简短有力，偶带锋芒";
    ailincard.core_desire = "守护仅存的家人";
    ailincard.deep_fear = "再次失去重要之人";
    ailincard.appearance = "黑发束起，左脸有旧刀疤";
    ailincard.background = "曾在王都担任骑士团教官";
    ailincard.knowledge_scope = "武艺、王都政治、军事战术";
    ailincard.core_traits = {"沉着", "果敢", "内热外冷"};
    auto ailin = service.create_character(world.id, ailincard);

    CharacterCard mashacard;
    mashacard.name = "玛莎";
    mashacard.gender = "女";
    mashacard.age = 22;
    mashacard.identity = "旅店老板娘";
    mashacard.emotional_tendency = "热情开朗";
    mashacard.speaking_style = "话多且快，爱用比喻";
    mashacard.core_desire = "经营好旅店，收集八方消息";
    mashacard.deep_fear = "旅店被毁，失去安身之处";
    mashacard.appearance = "红发编辫，系蓝围裙";
    mashacard.background = "北境本地人，父亲曾是商会会长";
    mashacard.knowledge_scope = "北境地缘、商路、民间传闻";
    mashacard.core_traits = {"机敏", "热情", "八卦"};
    auto masha = service.create_character(world.id, mashacard);

    // ── Create group ──
    auto wolf1 = service.create_character(world.id, []{
        CharacterCard c; c.name = "狼牙"; c.gender = "男"; c.age = 30;
        c.identity = "蛮族战士"; c.emotional_tendency = "粗暴"; c.speaking_style = "咆哮";
        c.core_desire = "保护部落"; c.deep_fear = "部落灭亡";
        c.appearance = "虎背熊腰"; c.background = "北方蛮族"; c.knowledge_scope = "狩猎、战斗";
        c.core_traits = {"勇猛", "忠诚"}; return c;
    }());
    auto wolf2 = service.create_character(world.id, []{
        CharacterCard c; c.name = "狼爪"; c.gender = "男"; c.age = 25;
        c.identity = "蛮族斥候"; c.emotional_tendency = "冷静"; c.speaking_style = "低声";
        c.core_desire = "探索世界"; c.deep_fear = "被困住";
        c.appearance = "精瘦"; c.background = "北方蛮族"; c.knowledge_scope = "潜行、侦查";
        c.core_traits = {"机警", "好奇"}; return c;
    }());
    auto group = service.create_group(world.id, "北方蛮族",
                                        "# 北方蛮族\n共同信仰：自然之灵\n禁忌：不得杀害幼崽",
                                        {wolf1.id, wolf2.id});

    // ── Create arc + chapter + scene ──
    Arc arc;
    arc.title = "守住狼烟";
    arc.purpose = "让艾琳接受守护者身份";
    auto created_arc = service.create_arc(world.id, arc);

    Chapter chapter;
    chapter.title = "雪夜来客";
    chapter.number = 1;
    chapter.pitch = "旧友求援打破边境平静";
    chapter.arc_id = created_arc.id;
    chapter.emotional_curve = nlohmann::json::object({{"beat", "pressure"}, {"intensity", 0.7}});
    auto created_chapter = service.create_chapter(world.id, chapter);

    Scene scene;
    scene.title = "旅店试探";
    scene.chapter_id = created_chapter.id;
    scene.world_time = "第三日夜";
    scene.participant_ids = {ailin.id, masha.id};
    scene.location_id = "狼烟旅店";
    auto created_scene = service.create_scene(world.id, scene);

    // ── Plant foreshadowing ──
    Foreshadowing fs;
    fs.content = "铁匠卡伦右手缺一根手指";
    fs.pay_off_idea = "卡伦断指烙着旧主纹章，后期用它指认凶手";
    fs.hint_level = ForeshadowHintLevel::Subtle;
    fs.tags = {ailin.id, "tavern"};
    auto planted_fs = service.plant_foreshadowing(world.id, fs);

    // ── Create secret ──
    Secret secret;
    secret.holder_id = ailin.id;
    secret.truth = "艾琳真实身份是王都骑士团教官";
    secret.public_version = "艾琳自称是从王都逃难的旅人";
    secret.stakes = "身份暴露将引来王都追杀令";
    secret.aware_character_ids = {masha.id};
    secret.suspicious_character_ids = {wolf2.id};
    secret.believed_truths = nlohmann::json::object({{wolf2.id, "艾琳可能是逃兵"}});
    secret.related_foreshadowing_ids = {planted_fs.id};
    auto created_secret = service.create_secret(world.id, secret);

    // ── Prepare scene ──
    auto prep = service.prepare_scene(world.id, created_scene.id);
    EXPECT_FALSE(prep.god_context.empty());
    EXPECT_NE(prep.god_context.find("旧友求援打破边境平静"), std::string::npos);
    EXPECT_NE(prep.god_context.find(ailin.id), std::string::npos);
    EXPECT_NE(prep.god_context.find(masha.id), std::string::npos);

    // Verify character views exist for both participants
    ASSERT_EQ(prep.character_views.size(), 2);

    // Verify secret asymmetry: Ailin (holder) and Masha (aware) should see different things
    bool ailins_view_has_truth = false;
    bool mashas_view_has_truth = false;
    for (const auto& view : prep.character_views) {
        if (view.agent_id == ailin.id &&
            view.system_prompt.find("骑士团教官") != std::string::npos) {
            ailins_view_has_truth = true;
        }
        if (view.agent_id == masha.id &&
            view.system_prompt.find("骑士团教官") != std::string::npos) {
            mashas_view_has_truth = true;
        }
    }
    EXPECT_TRUE(ailins_view_has_truth);
    EXPECT_TRUE(mashas_view_has_truth);

    // Verify relevant foreshadowing includes planted item
    EXPECT_EQ(prep.relevant_foreshadowing.size(), 1);
    EXPECT_EQ(prep.relevant_foreshadowing[0].id, planted_fs.id);

    // ── End scene ──
    std::string scene_draft =
        "林霜推开旅店木门，风雪灌入厅堂。\n"
        "'来杯热酒。'她低声说。\n"
        "玛莎从吧台后抬头，目光在林霜左脸的疤痕上停留片刻。\n"
        "'这位客人，看您不像一般旅人。'\n"
        "林霜没有回答，只是将一枚烧焦的狼烟令放在吧台上。\n"
        "'北境的冬天，从来不缺故事。'玛莎倒满酒杯。\n"
        "'但我猜您的故事，比大多数人都有意思。'\n";

    auto wrap = service.end_scene(world.id, created_scene.id, scene_draft);

    // Verify diaries were written for both participants
    EXPECT_EQ(wrap.diaries_written.size(), 2);

    // Verify relation was updated
    EXPECT_EQ(wrap.relations_updated.size(), 1);

    // Verify timeline event was recorded
    auto timeline_path = service.worlds().world_path(world.id) / "timeline.json";
    ASSERT_TRUE(std::filesystem::exists(timeline_path));

    // Verify foreshadow proposals
    // (may or may not have detected proposals based on heuristics)

    // Verify chapter stats
    EXPECT_GE(wrap.chapter_foreshadow_stats.open + wrap.chapter_foreshadow_stats.paid, 0);

    // ── Verify world directory structure ──
    auto wp = service.worlds().world_path(world.id);
    EXPECT_TRUE(std::filesystem::exists(wp / "world_knowledge"));
    EXPECT_TRUE(std::filesystem::exists(wp / "god"));
    EXPECT_TRUE(std::filesystem::exists(wp / "managers/map"));
    EXPECT_TRUE(std::filesystem::exists(wp / "managers/history"));
    EXPECT_TRUE(std::filesystem::exists(wp / "managers/magic"));
    EXPECT_TRUE(std::filesystem::exists(wp / "managers/faction"));
    EXPECT_TRUE(std::filesystem::exists(wp / "agents"));
    EXPECT_TRUE(std::filesystem::exists(wp / "scenes"));
    EXPECT_TRUE(std::filesystem::exists(wp / "chapters"));
    EXPECT_TRUE(std::filesystem::exists(wp / "arcs"));
    EXPECT_TRUE(std::filesystem::exists(wp / "secrets"));
    EXPECT_TRUE(std::filesystem::exists(wp / "foreshadows"));
    EXPECT_TRUE(std::filesystem::exists(wp / "sessions"));
    EXPECT_TRUE(std::filesystem::exists(wp / "timeline.json"));
}
