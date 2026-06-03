#include <gtest/gtest.h>
#include <merak/worldbuilding/agent_store.hpp>
#include <merak/worldbuilding/ids.hpp>
#include <merak/worldbuilding/sqlite_helpers.hpp>
#include <merak/worldbuilding/world_store.hpp>

#include <algorithm>
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
    card.speaking_style = "短句，少废话\n紧张时会重复确认方位";
    card.taboo_topics = {"叛逃的兄长"};
    card.core_desire = "守住北境";
    card.deep_fear = "再次失去同伴";
    card.daily_goal = "巡查狼烟塔";
    card.background = "雪线村幸存者。\n身份：曾被敌军误认为逃兵。\n曾在狼烟塔独守三夜。";
    card.knowledge_scope = "北境地形、哨站暗号\n不知道王都密约。";
    card.relations = nlohmann::json::object({{"陆峥", "战友"}});
    card.appearance = "灰斗篷，左手有旧伤\n习惯摸刀柄确认距离";
    return card;
}

void expect_character_card_matches(const CharacterCard& actual,
                                   const CharacterCard& expected) {
    EXPECT_EQ(actual.agent_id, expected.agent_id);
    EXPECT_EQ(actual.name, expected.name);
    EXPECT_EQ(actual.age, expected.age);
    EXPECT_EQ(actual.gender, expected.gender);
    EXPECT_EQ(actual.race, expected.race);
    EXPECT_EQ(actual.identity, expected.identity);
    EXPECT_EQ(actual.core_traits, expected.core_traits);
    EXPECT_EQ(actual.emotional_tendency, expected.emotional_tendency);
    EXPECT_EQ(actual.speaking_style, expected.speaking_style);
    EXPECT_EQ(actual.taboo_topics, expected.taboo_topics);
    EXPECT_EQ(actual.core_desire, expected.core_desire);
    EXPECT_EQ(actual.deep_fear, expected.deep_fear);
    EXPECT_EQ(actual.daily_goal, expected.daily_goal);
    EXPECT_EQ(actual.background, expected.background);
    EXPECT_EQ(actual.knowledge_scope, expected.knowledge_scope);
    EXPECT_EQ(actual.relations, expected.relations);
    EXPECT_EQ(actual.appearance, expected.appearance);
    EXPECT_EQ(actual.version, expected.version);
    EXPECT_EQ(actual.updated_at, expected.updated_at);
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
    for (const auto& label : std::vector<std::string>{
             "姓名：", "年龄：", "性别：", "种族：", "身份：",
             "核心性格特质：", "情绪倾向：", "说话风格：", "禁忌话题：",
             "核心欲望：", "深层恐惧：", "日常目标：", "背景故事：",
             "知识范围：", "人际关系：", "外貌与习惯："}) {
        EXPECT_NE(card_markdown.find(label), std::string::npos)
            << "missing label " << label;
    }
    EXPECT_NE(card_markdown.find("姓名：林霜"), std::string::npos);
    EXPECT_NE(card_markdown.find("核心性格特质：谨慎、护短"),
              std::string::npos);
    EXPECT_NE(card_markdown.find("禁忌话题：叛逃的兄长"), std::string::npos);

    auto expected = sample_card();
    expected.agent_id = record.id;
    expected.version = 1;
    expected.updated_at = record.updated_at;
    expect_character_card_matches(agents.load_character_card(record.id),
                                  expected);

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
    expect_character_card_matches(agents.load_character_card(record.id),
                                  updated);

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
    EXPECT_FALSE(agents.can_speak_directly(group.id));
    {
        {
            SqliteDb db((root / "worlds.sqlite3").string());
            Statement remove_metadata(
                db, "DELETE FROM agent_metadata WHERE agent_id = ?1");
            bind_text(remove_metadata, 1, group.id);
            EXPECT_FALSE(remove_metadata.step());
        }
        EXPECT_THROW(agents.can_speak_directly(group.id), std::runtime_error);
    }
    EXPECT_EQ(profile["member_agent_ids"].size(), 2);
    EXPECT_EQ(profile["shared_memory_ids"].size(), 1);

    const auto expected_refs =
        std::vector<std::string>({"shared_memory:" + group.id});
    EXPECT_EQ(agents.shared_memory_refs_for(a.id), expected_refs);
    EXPECT_EQ(agents.shared_memory_refs_for(b.id), expected_refs);
    EXPECT_TRUE(std::filesystem::exists(worlds.world_path(world.id) /
                                        "agents" / a.id /
                                        "group_memory_refs.json"));
    EXPECT_TRUE(std::filesystem::exists(worlds.world_path(world.id) /
                                        "agents" / b.id /
                                        "group_memory_refs.json"));
    EXPECT_FALSE(std::filesystem::exists(worlds.world_path(world.id) /
                                         "agents" / a.id / "shared_memory"));
    EXPECT_FALSE(std::filesystem::exists(worlds.world_path(world.id) /
                                         "agents" / b.id / "shared_memory"));
    EXPECT_FALSE(std::filesystem::exists(worlds.world_path(world.id) /
                                         "agents" / a.id / "diary" /
                                         "shared_memory.md"));
    EXPECT_FALSE(std::filesystem::exists(worlds.world_path(world.id) /
                                         "agents" / b.id / "diary" /
                                         "shared_memory.md"));
}

TEST(AgentStore, CreateGroupValidatesMembersBeforeCreatingAnyArtifacts) {
    auto root = temp_dir();
    WorldStore worlds(root);
    auto world = worlds.create_world("北境", "雪原史诗");
    auto other_world = worlds.create_world("南境", "海港史诗");
    AgentStore agents(worlds, root);
    auto member = agents.create_character(world.id, sample_card());
    auto other_member = agents.create_character(other_world.id, sample_card());

    EXPECT_THROW(agents.create_group(world.id, "坏小队", "不应写入",
                                     {member.id, "agent_missing"}),
                 std::runtime_error);
    EXPECT_THROW(agents.create_group(world.id, "跨界小队", "不应写入",
                                     {member.id, other_member.id}),
                 std::runtime_error);

    auto agents_after = agents.list_agents(world.id);
    EXPECT_EQ(std::count_if(agents_after.begin(), agents_after.end(),
                            [](const AgentRecord& record) {
                                return record.kind == AgentKind::Group;
                            }),
              0);
    EXPECT_FALSE(std::filesystem::exists(worlds.world_path(world.id) /
                                         "agents" / member.id /
                                         "group_memory_refs.json"));
}

TEST(AgentStore, CreateGroupCleansUpDbFilesAndMemberRefsOnFilesystemFailure) {
    auto root = temp_dir();
    WorldStore worlds(root);
    auto world = worlds.create_world("北境", "雪原史诗");
    AgentStore agents(worlds, root);
    auto member = agents.create_character(world.id, sample_card());
    auto member_path = worlds.world_path(world.id) / "agents" / member.id;
    std::filesystem::remove(member_path / "memory_index.md");
    std::filesystem::create_directory(member_path / "memory_index.md");

    EXPECT_THROW(agents.create_group(world.id, "坏小队", "不应留下痕迹",
                                     {member.id}),
                 std::runtime_error);

    const auto agents_after = agents.list_agents(world.id);
    EXPECT_EQ(std::count_if(agents_after.begin(), agents_after.end(),
                            [](const AgentRecord& record) {
                                return record.kind == AgentKind::Group;
                            }),
              0);
    EXPECT_FALSE(std::filesystem::exists(member_path / "group_memory_refs.json"));
    for (const auto& entry :
         std::filesystem::directory_iterator(worlds.world_path(world.id) /
                                             "agents")) {
        EXPECT_FALSE(std::filesystem::exists(entry.path() /
                                             "group_profile.json"));
    }
}

TEST(AgentStore, AppendDiaryEntryWritesSceneDiaryAndUpdatesMemoryIndex) {
    auto root = temp_dir();
    WorldStore worlds(root);
    auto world = worlds.create_world("北境", "雪原史诗");
    AgentStore agents(worlds, root);
    auto record = agents.create_character(world.id, sample_card());

    agents.append_diary_entry({"diary_first", record.id, "scene_gate",
                               "第三日夜", "发现城门下的旧血迹。", ""});
    agents.append_diary_entry({"diary_second", record.id, "scene_gate",
                               "第三日夜", "第二次确认血迹来自城内。", ""});

    auto agent_path = worlds.world_path(world.id) / "agents" / record.id;
    auto first_diary_path = agent_path / "diary" / "diary_first.md";
    auto second_diary_path = agent_path / "diary" / "diary_second.md";
    ASSERT_TRUE(std::filesystem::exists(first_diary_path));
    ASSERT_TRUE(std::filesystem::exists(second_diary_path));
    EXPECT_NE(slurp(first_diary_path).find("发现城门下的旧血迹。"),
              std::string::npos);
    EXPECT_NE(slurp(second_diary_path).find("第二次确认血迹来自城内。"),
              std::string::npos);
    EXPECT_NE(slurp(agent_path / "memory_index.md").find("diary/diary_first.md"),
              std::string::npos);
    EXPECT_NE(
        slurp(agent_path / "memory_index.md").find("diary/diary_second.md"),
        std::string::npos);
    EXPECT_NE(slurp(agent_path / "memory_index.md").find("scene_gate"),
              std::string::npos);

    auto recent = agents.recent_diary(record.id, 1);
    ASSERT_EQ(recent.size(), 1);
    EXPECT_EQ(recent[0].scene_id, "scene_gate");
    EXPECT_EQ(recent[0].id, "diary_second");
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

TEST(AgentStore, UpsertRelationRejectsCrossWorldAgents) {
    auto root = temp_dir();
    WorldStore worlds(root);
    auto world = worlds.create_world("北境", "雪原史诗");
    auto other_world = worlds.create_world("南境", "海港史诗");
    AgentStore agents(worlds, root);
    auto source = agents.create_character(world.id, sample_card());
    auto target = agents.create_character(other_world.id, sample_card());

    EXPECT_THROW(agents.upsert_relation(RelationEntry{
                     .agent_id = source.id,
                     .target_id = target.id,
                     .relation_type = "误连",
                     .description = "跨世界关系不允许",
                     .updated_at = "",
                     .intimacy = 0,
                     .key_events = {},
                 }),
                 std::runtime_error);
    EXPECT_TRUE(agents.relations_for(source.id).empty());
}
