#include <gtest/gtest.h>
#include <merak/worldbuilding/agent_store.hpp>
#include <merak/worldbuilding/ids.hpp>
#include <merak/worldbuilding/world_store.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace merak::worldbuilding;

namespace {

std::filesystem::path temp_dir() {
    auto path =
        std::filesystem::temp_directory_path() / make_id("agent_store_test");
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

std::string slurp(const std::filesystem::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

CharacterCard sample_card() {
    CharacterCard card;
    card.name = "林霜";
    card.age = 28;
    card.gender = "女";
    card.race = "人类";
    card.identity = "边境斥候";
    card.core_traits = {"谨慎", "护短"};
    card.emotional_tendency = "压抑但敏锐";
    card.speaking_style = "短句，少废话";
    card.taboo_topics = {"叛逃的兄长"};
    card.core_desire = "守住北境";
    card.deep_fear = "再次失去同伴";
    card.daily_goal = "巡查狼烟塔";
    card.background = "雪线村幸存者。";
    card.knowledge_scope = "北境地形、哨站暗号";
    card.relations = nlohmann::json::object({{"陆峥", "战友"}});
    card.appearance = "灰斗篷，左手有旧伤";
    return card;
}

} // namespace

TEST(AgentStore, CreateManagerStoresProfileUnderManagerDomain) {
    auto root = temp_dir();
    WorldStore worlds(root);
    worlds.initialize();
    auto world = worlds.create_world("北境", "雪原史诗");
    AgentStore agents(worlds, root);

    auto manager = agents.create_manager(world.id, AgentKind::MapManager, "map",
                                         "维护地图知识");

    EXPECT_EQ(manager.kind, AgentKind::MapManager);
    EXPECT_EQ(manager.world_id, world.id);
    auto profile_path = worlds.world_path(world.id) / "managers/map/profile.json";
    ASSERT_TRUE(std::filesystem::exists(profile_path));
    auto profile = nlohmann::json::parse(slurp(profile_path));
    EXPECT_EQ(profile["agent_id"], manager.id);
    EXPECT_EQ(profile["domain"], "map");
    EXPECT_EQ(profile["instructions"], "维护地图知识");
}

TEST(AgentStore, CreateCharacterWritesCardHistoryAndMemoryLifecyclePaths) {
    auto root = temp_dir();
    WorldStore worlds(root);
    auto world = worlds.create_world("北境", "雪原史诗");
    AgentStore agents(worlds, root);

    auto record = agents.create_character(world.id, sample_card());
    auto agent_path = worlds.world_path(world.id) / "agents" / record.id;

    EXPECT_EQ(record.kind, AgentKind::Individual);
    EXPECT_TRUE(std::filesystem::exists(agent_path / "character_card.md"));
    EXPECT_TRUE(std::filesystem::exists(agent_path / "diary"));
    EXPECT_TRUE(std::filesystem::exists(agent_path / "summaries"));
    EXPECT_TRUE(std::filesystem::exists(agent_path / "memory_index.md"));
    EXPECT_TRUE(std::filesystem::exists(agent_path / "relations.md"));

    auto card_markdown = slurp(agent_path / "character_card.md");
    EXPECT_NE(card_markdown.find("姓名：林霜"), std::string::npos);
    EXPECT_NE(card_markdown.find("年龄：28"), std::string::npos);
    EXPECT_NE(card_markdown.find("核心性格特质：谨慎、护短"),
              std::string::npos);
    EXPECT_NE(card_markdown.find("外貌与习惯：灰斗篷，左手有旧伤"),
              std::string::npos);

    int history_count = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(agent_path /
                                             "character_card_history")) {
        if (entry.path().filename().string().ends_with("-v1.md")) {
            ++history_count;
        }
    }
    EXPECT_EQ(history_count, 1);
}

TEST(AgentStore, UpdateCharacterCardIncrementsVersionAndPreservesHistory) {
    auto root = temp_dir();
    WorldStore worlds(root);
    auto world = worlds.create_world("北境", "雪原史诗");
    AgentStore agents(worlds, root);

    auto record = agents.create_character(world.id, sample_card());
    auto next = agents.load_character_card(record.id);
    next.daily_goal = "追踪失踪商队";

    auto updated = agents.update_character_card(record.id, next, "剧情推进");

    EXPECT_EQ(updated.version, 2);
    EXPECT_EQ(agents.load_character_card(record.id).daily_goal, "追踪失踪商队");

    auto history_path = worlds.world_path(world.id) / "agents" / record.id /
                        "character_card_history";
    bool has_v1 = false;
    bool has_v2 = false;
    for (const auto& entry : std::filesystem::directory_iterator(history_path)) {
        const auto filename = entry.path().filename().string();
        has_v1 = has_v1 || filename.ends_with("-v1.md");
        has_v2 = has_v2 || filename.ends_with("-v2.md");
    }
    EXPECT_TRUE(has_v1);
    EXPECT_TRUE(has_v2);
}

TEST(AgentStore, CreateGroupStoresProfileMembersAndSharedMemoryRefs) {
    auto root = temp_dir();
    WorldStore worlds(root);
    auto world = worlds.create_world("北境", "雪原史诗");
    AgentStore agents(worlds, root);
    auto a = agents.create_character(world.id, sample_card());
    auto card = sample_card();
    card.name = "陆峥";
    auto b = agents.create_character(world.id, card);

    auto group = agents.create_group(world.id, "狼烟小队", "以守望互保为荣",
                                     {a.id, b.id});
    auto group_path = worlds.world_path(world.id) / "agents" / group.id;

    EXPECT_EQ(group.kind, AgentKind::Group);
    ASSERT_TRUE(std::filesystem::exists(group_path / "group_profile.json"));
    EXPECT_TRUE(std::filesystem::exists(group_path / "culture_card.md"));
    EXPECT_TRUE(std::filesystem::exists(group_path / "members.json"));
    EXPECT_TRUE(std::filesystem::exists(group_path / "shared_memory_refs.json"));

    auto profile = nlohmann::json::parse(slurp(group_path /
                                               "group_profile.json"));
    EXPECT_EQ(profile["can_speak_directly"], false);
    EXPECT_EQ(profile["member_agent_ids"].size(), 2);
    EXPECT_EQ(profile["shared_memory_ids"].size(), 1);

    EXPECT_FALSE(std::filesystem::exists(worlds.world_path(world.id) /
                                         "agents" / a.id / "shared_memory"));
    EXPECT_FALSE(std::filesystem::exists(worlds.world_path(world.id) /
                                         "agents" / b.id / "shared_memory"));
}

TEST(AgentStore, AppendDiaryEntryWritesSceneDiaryAndUpdatesMemoryIndex) {
    auto root = temp_dir();
    WorldStore worlds(root);
    auto world = worlds.create_world("北境", "雪原史诗");
    AgentStore agents(worlds, root);
    auto record = agents.create_character(world.id, sample_card());

    agents.append_diary_entry({"", record.id, "scene_gate", "第三日夜",
                               "发现城门下的旧血迹。", ""});

    auto agent_path = worlds.world_path(world.id) / "agents" / record.id;
    auto diary_path = agent_path / "diary" / "scene_gate.md";
    ASSERT_TRUE(std::filesystem::exists(diary_path));
    EXPECT_NE(slurp(diary_path).find("发现城门下的旧血迹。"), std::string::npos);
    EXPECT_NE(slurp(agent_path / "memory_index.md").find("scene_gate"),
              std::string::npos);

    auto recent = agents.recent_diary(record.id, 1);
    ASSERT_EQ(recent.size(), 1);
    EXPECT_EQ(recent[0].scene_id, "scene_gate");
}

TEST(AgentStore, UpsertRelationClampsIntimacyAndStoresKeyEvents) {
    auto root = temp_dir();
    WorldStore worlds(root);
    auto world = worlds.create_world("北境", "雪原史诗");
    AgentStore agents(worlds, root);
    auto source = agents.create_character(world.id, sample_card());
    auto target_card = sample_card();
    target_card.name = "陆峥";
    auto target = agents.create_character(world.id, target_card);

    agents.upsert_relation(RelationEntry{
        .agent_id = source.id,
        .target_id = target.id,
        .relation_type = "战友",
        .description = "共同守城",
        .updated_at = "",
        .intimacy = 145,
        .key_events = {"雪夜救援", "城门伏击"},
    });

    auto relations = agents.relations_for(source.id);
    ASSERT_EQ(relations.size(), 1);
    EXPECT_EQ(relations[0].target_id, target.id);
    EXPECT_EQ(relations[0].intimacy, 100);
    EXPECT_EQ(relations[0].key_events,
              std::vector<std::string>({"雪夜救援", "城门伏击"}));

    auto relation_markdown = slurp(worlds.world_path(world.id) / "agents" /
                                   source.id / "relations.md");
    EXPECT_NE(relation_markdown.find("亲密度：100"), std::string::npos);
    EXPECT_NE(relation_markdown.find("雪夜救援"), std::string::npos);
}
