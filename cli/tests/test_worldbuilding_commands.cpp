#include <gtest/gtest.h>
#include "../src/commands/worldbuilding_commands.hpp"

using namespace merak::commands;

TEST(WorldbuildingCommands, ParseWorldList) {
    auto cmd = parse_worldbuilding_command("/world list", "", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::WorldList);
}

TEST(WorldbuildingCommands, ParseWorldCreate) {
    auto cmd = parse_worldbuilding_command("/world create 北境", "", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::WorldCreate);
    ASSERT_EQ(cmd->args.size(), 1);
    EXPECT_EQ(cmd->args[0], "北境");
}

TEST(WorldbuildingCommands, ParseWorldUse) {
    auto cmd = parse_worldbuilding_command("/world use world_abc", "", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::WorldUse);
    EXPECT_EQ(cmd->args[0], "world_abc");
}

TEST(WorldbuildingCommands, ParseWorldDelete) {
    auto cmd = parse_worldbuilding_command("/world delete world_xyz", "", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::WorldDelete);
}

TEST(WorldbuildingCommands, ParseAgentList) {
    auto cmd = parse_worldbuilding_command("/agent list", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::AgentList);
}

TEST(WorldbuildingCommands, ParseAgentCreateCharacter) {
    auto cmd = parse_worldbuilding_command("/agent create character 林霜", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::AgentCreate);
    ASSERT_GE(cmd->args.size(), 2);
    EXPECT_EQ(cmd->args[0], "character");
    EXPECT_EQ(cmd->args[1], "林霜");
}

TEST(WorldbuildingCommands, ParseAgentCreateManager) {
    auto cmd = parse_worldbuilding_command("/agent create manager map 地图管理者", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::AgentCreate);
    EXPECT_EQ(cmd->args[0], "manager");
    EXPECT_EQ(cmd->args[1], "map");
}

TEST(WorldbuildingCommands, ParseAgentEdit) {
    auto cmd = parse_worldbuilding_command("/agent edit agent_abc", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::AgentEdit);
    EXPECT_EQ(cmd->args[0], "agent_abc");
}

TEST(WorldbuildingCommands, ParseAgentHistory) {
    auto cmd = parse_worldbuilding_command("/agent history agent_abc", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::AgentHistory);
}

TEST(WorldbuildingCommands, ParseAgentDelete) {
    auto cmd = parse_worldbuilding_command("/agent delete agent_abc", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::AgentDelete);
}

TEST(WorldbuildingCommands, ParseAgentRoute) {
    auto cmd = parse_worldbuilding_command("@agent_linshi 你好", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::AgentRoute);
    EXPECT_EQ(cmd->args[0], "agent_linshi");
    EXPECT_EQ(cmd->args[1], "你好");
}

TEST(WorldbuildingCommands, ParseClear) {
    auto cmd = parse_worldbuilding_command("@clear", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::AgentRoute);
}

TEST(WorldbuildingCommands, ParseStoryOverview) {
    auto cmd = parse_worldbuilding_command("/story overview", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::StoryOverview);
}

TEST(WorldbuildingCommands, ParseChapterNew) {
    auto cmd = parse_worldbuilding_command("/chapter new 雪夜来客", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::ChapterNew);
    EXPECT_EQ(cmd->args[0], "雪夜来客");
}

TEST(WorldbuildingCommands, ParseChapterUse) {
    auto cmd = parse_worldbuilding_command("/chapter use chapter_1", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::ChapterUse);
}

TEST(WorldbuildingCommands, ParseChapterList) {
    auto cmd = parse_worldbuilding_command("/chapter list", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::ChapterList);
}

TEST(WorldbuildingCommands, ParseChapterCurve) {
    auto cmd = parse_worldbuilding_command("/chapter curve", "w1", "ch1", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::ChapterCurve);
}

TEST(WorldbuildingCommands, ParseArcNew) {
    auto cmd = parse_worldbuilding_command("/arc new 守城之战", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::ArcNew);
}

TEST(WorldbuildingCommands, ParseArcList) {
    auto cmd = parse_worldbuilding_command("/arc list", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::ArcList);
}

TEST(WorldbuildingCommands, ParseSceneNew) {
    auto cmd = parse_worldbuilding_command("/scene new 旅店试探", "w1", "ch1", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::SceneNew);
}

TEST(WorldbuildingCommands, ParseSceneList) {
    auto cmd = parse_worldbuilding_command("/scene list", "w1", "ch1", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::SceneList);
}

TEST(WorldbuildingCommands, ParseSceneUse) {
    auto cmd = parse_worldbuilding_command("/scene use scene_1", "w1", "ch1", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::SceneUse);
}

TEST(WorldbuildingCommands, ParseSceneEnd) {
    auto cmd = parse_worldbuilding_command("/scene end", "w1", "ch1", "scene_1");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::SceneEnd);
}

TEST(WorldbuildingCommands, ParseSceneJump) {
    auto cmd = parse_worldbuilding_command("/scene jump 第四日午", "w1", "ch1", "scene_1");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::SceneJump);
    EXPECT_EQ(cmd->args[0], "第四日午");
}

TEST(WorldbuildingCommands, ParseTimeNow) {
    auto cmd = parse_worldbuilding_command("/time now", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::TimeNow);
}

TEST(WorldbuildingCommands, ParseTimeAdvance) {
    auto cmd = parse_worldbuilding_command("/time advance 2h", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::TimeAdvance);
    EXPECT_EQ(cmd->args[0], "2h");
}

TEST(WorldbuildingCommands, ParseTimeCalendar) {
    auto cmd = parse_worldbuilding_command("/time calendar", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::TimeCalendar);
}

TEST(WorldbuildingCommands, ParseForeshadowList) {
    auto cmd = parse_worldbuilding_command("/foreshadow list", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::ForeshadowList);
}

TEST(WorldbuildingCommands, ParseForeshadowPlant) {
    auto cmd = parse_worldbuilding_command("/foreshadow plant 铁匠的断指", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::ForeshadowPlant);
    EXPECT_EQ(cmd->args[0], "铁匠的断指");
}

TEST(WorldbuildingCommands, ParseForeshadowPay) {
    auto cmd = parse_worldbuilding_command("/foreshadow pay fs_1", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::ForeshadowPay);
}

TEST(WorldbuildingCommands, ParseForeshadowAbandon) {
    auto cmd = parse_worldbuilding_command("/foreshadow abandon fs_1", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::ForeshadowAbandon);
}

TEST(WorldbuildingCommands, ParseForeshadowCheck) {
    auto cmd = parse_worldbuilding_command("/foreshadow check", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::ForeshadowCheck);
}

TEST(WorldbuildingCommands, ParseForeshadowStats) {
    auto cmd = parse_worldbuilding_command("/foreshadow stats", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::ForeshadowStats);
}

TEST(WorldbuildingCommands, ParseSecretList) {
    auto cmd = parse_worldbuilding_command("/secret list", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::SecretList);
}

TEST(WorldbuildingCommands, ParseSecretCreate) {
    auto cmd = parse_worldbuilding_command("/secret create 艾琳的真实身份", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::SecretCreate);
}

TEST(WorldbuildingCommands, ParseSecretExpose) {
    auto cmd = parse_worldbuilding_command("/secret expose secret_1", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::SecretExpose);
}

TEST(WorldbuildingCommands, ParseSecretCheck) {
    auto cmd = parse_worldbuilding_command("/secret check @A @B", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::SecretCheck);
}

TEST(WorldbuildingCommands, ParseVoiceCheck) {
    auto cmd = parse_worldbuilding_command("/voice check", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::VoiceCheck);
}

TEST(WorldbuildingCommands, ParseVoiceGroup) {
    auto cmd = parse_worldbuilding_command("/voice group", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::VoiceGroup);
}

TEST(WorldbuildingCommands, ParseVoiceCompare) {
    auto cmd = parse_worldbuilding_command("/voice compare @A @B", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::VoiceCompare);
}

TEST(WorldbuildingCommands, ParseVoiceAt) {
    auto cmd = parse_worldbuilding_command("/voice @agent_linshi", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::VoiceAt);
}

TEST(WorldbuildingCommands, ParseMemoryLatest) {
    auto cmd = parse_worldbuilding_command("/memory @agent_linshi latest", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::MemoryLatest);
}

TEST(WorldbuildingCommands, ParseMemorySearch) {
    auto cmd = parse_worldbuilding_command("/memory @agent_linshi search 童年", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::MemorySearch);
}

TEST(WorldbuildingCommands, ParseDiaryShow) {
    auto cmd = parse_worldbuilding_command("/diary @agent_linshi show", "w1", "", "");
    ASSERT_TRUE(cmd.has_value());
    EXPECT_EQ(cmd->action, WorldbuildingAction::DiaryShow);
}

TEST(WorldbuildingCommands, NonWorldbuildingCommandReturnsNullopt) {
    auto cmd = parse_worldbuilding_command("/help", "", "", "");
    EXPECT_FALSE(cmd.has_value());
}

TEST(WorldbuildingCommands, MissingWorldContextDoesNotCallHttp) {
    WorldbuildingCommand cmd;
    cmd.action = WorldbuildingAction::AgentList;
    bool called = false;
    auto result = execute_worldbuilding_command(cmd,
        [&called](const std::string&, const std::string&, const nlohmann::json&) {
            called = true;
            return nlohmann::json::object();
        });
    EXPECT_FALSE(called);
    EXPECT_NE(result.find("select a world first"), std::string::npos);
}

TEST(WorldbuildingCommands, WorldDeleteUsesDeleteRoute) {
    WorldbuildingCommand cmd;
    cmd.action = WorldbuildingAction::WorldDelete;
    cmd.args = {"world_xyz"};
    std::string method_seen;
    std::string path_seen;
    auto result = execute_worldbuilding_command(cmd,
        [&method_seen, &path_seen](const std::string& method, const std::string& path,
                                   const nlohmann::json&) {
            method_seen = method;
            path_seen = path;
            return nlohmann::json{{"ok", true}};
        });
    EXPECT_EQ(method_seen, "DELETE");
    EXPECT_EQ(path_seen, "/api/worldbuilding/worlds/world_xyz");
    EXPECT_NE(result.find("\"ok\": true"), std::string::npos);
}

TEST(WorldbuildingCommands, HelpTextNotEmpty) {
    auto help = worldbuilding_help_text();
    EXPECT_FALSE(help.empty());
    EXPECT_NE(help.find("/world"), std::string::npos);
    EXPECT_NE(help.find("/agent"), std::string::npos);
    EXPECT_NE(help.find("/scene"), std::string::npos);
    EXPECT_NE(help.find("/foreshadow"), std::string::npos);
    EXPECT_NE(help.find("/secret"), std::string::npos);
    EXPECT_NE(help.find("/voice"), std::string::npos);
}
