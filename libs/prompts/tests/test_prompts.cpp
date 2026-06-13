#include <gtest/gtest.h>
#include <merak/prompts/compositor.hpp>
#include <merak/prompts/team_prompt.hpp>
#include <merak/prompts/memory_prompt.hpp>
#include <merak/prompts/skill_prompt.hpp>
#include <merak/prompts/scene_prompt.hpp>
#include <merak/prompts/core_prompt.hpp>

using namespace merak::prompts;

// ─── PromptSection 排序 ───

TEST(PromptSectionSort, GlobalBeforeSessionBeforeNone) {
    std::vector<PromptSection> sections = {
        {"volatile", PromptCachePolicy::None},
        {"stable", PromptCachePolicy::Global},
        {"semi", PromptCachePolicy::Session},
    };
    PromptCompositor::sort_by_scope(sections);
    EXPECT_EQ(sections[0].cache_policy, PromptCachePolicy::Global);
    EXPECT_EQ(sections[1].cache_policy, PromptCachePolicy::Session);
    EXPECT_EQ(sections[2].cache_policy, PromptCachePolicy::None);
}

// ─── Memory Prompt ───

TEST(MemoryPrompt, NoneModeReturnsEmpty) {
    auto section = build_memory_section(MemoryPromptMode::None);
    EXPECT_TRUE(section.text.empty());
}

TEST(MemoryPrompt, MinimalModeHasRules) {
    auto section = build_memory_section(MemoryPromptMode::Minimal);
    EXPECT_NE(section.text.find("Memory"), std::string::npos);
    EXPECT_NE(section.text.find("记漏比记错划算"), std::string::npos);
    EXPECT_EQ(section.text.find("<types>"), std::string::npos);
}

// ─── Skill Prompt ───

TEST(SkillPrompt, ContainsMarkdownConstraint) {
    auto section = build_skill_section();
    EXPECT_NE(section.text.find("Markdown"), std::string::npos);
    EXPECT_NE(section.text.find("简洁"), std::string::npos);
    EXPECT_EQ(section.cache_policy, PromptCachePolicy::Global);
}

// ─── Team Coordination ───

TEST(TeamPrompt, FanOutIncludesSiblings) {
    TeamContext ctx;
    ctx.mode = CoordinationMode::FanOut;
    ctx.agent_id = "a";
    ctx.sibling_ids = {"a", "b", "c"};
    ctx.aggregation_strategy = "AllResults";

    auto result = build_team_coordination(ctx);
    EXPECT_NE(result.find("b"), std::string::npos);
    EXPECT_NE(result.find("c"), std::string::npos);
}

TEST(TeamPrompt, FanOutSoleAgent) {
    TeamContext ctx;
    ctx.mode = CoordinationMode::FanOut;
    ctx.agent_id = "x";
    ctx.sibling_ids = {"x"};

    auto result = build_team_coordination(ctx);
    EXPECT_NE(result.find("唯一"), std::string::npos);
}

TEST(TeamPrompt, FanOutWithGate) {
    TeamContext ctx;
    ctx.mode = CoordinationMode::FanOut;
    ctx.agent_id = "a";
    ctx.sibling_ids = {"a", "b"};
    ctx.aggregation_strategy = "Consensus";
    ctx.has_gate = true;

    auto result = build_team_coordination(ctx);
    EXPECT_NE(result.find("质量门"), std::string::npos);
}

TEST(TeamPrompt, PipelineFirstStage) {
    TeamContext ctx;
    ctx.mode = CoordinationMode::Pipeline;
    ctx.stage_index = 0;
    ctx.total_stages = 3;
    ctx.agent_id = "coder";

    auto result = build_team_coordination(ctx);
    EXPECT_NE(result.find("第一个"), std::string::npos);
    EXPECT_NE(result.find("3"), std::string::npos);
}

TEST(TeamPrompt, PipelineMiddleStage) {
    TeamContext ctx;
    ctx.mode = CoordinationMode::Pipeline;
    ctx.stage_index = 1;
    ctx.total_stages = 3;
    ctx.has_previous_output = true;
    ctx.has_gate = true;

    auto result = build_team_coordination(ctx);
    EXPECT_NE(result.find("2/3"), std::string::npos);
    EXPECT_NE(result.find("在其基础上构建"), std::string::npos);
    EXPECT_NE(result.find("质量门"), std::string::npos);
}

TEST(TeamPrompt, AdversarialProducerFirstRound) {
    TeamContext ctx;
    ctx.mode = CoordinationMode::AdversarialProducer;
    ctx.current_round = 0;
    ctx.max_rounds = 3;

    auto result = build_team_coordination(ctx);
    EXPECT_NE(result.find("1/3"), std::string::npos);
    EXPECT_EQ(result.find("修订指导"), std::string::npos);
}

TEST(TeamPrompt, AdversarialProducerRevisionRound) {
    TeamContext ctx;
    ctx.mode = CoordinationMode::AdversarialProducer;
    ctx.current_round = 1;
    ctx.max_rounds = 3;
    ctx.has_feedback = true;
    ctx.has_gate = true;

    auto result = build_team_coordination(ctx);
    EXPECT_NE(result.find("2/3"), std::string::npos);
    EXPECT_NE(result.find("修订指导"), std::string::npos);
}

TEST(TeamPrompt, AdversarialReviewerFormat) {
    TeamContext ctx;
    ctx.mode = CoordinationMode::AdversarialReviewer;
    ctx.agent_id = "coder-1";
    ctx.current_round = 0;
    ctx.max_rounds = 3;

    auto result = build_team_coordination(ctx);
    EXPECT_NE(result.find("审查者"), std::string::npos);
    EXPECT_NE(result.find("APPROVE"), std::string::npos);
    EXPECT_NE(result.find("NEEDS_REVISION"), std::string::npos);
    EXPECT_NE(result.find("REJECT"), std::string::npos);
}

TEST(TeamPrompt, ForkChild) {
    TeamContext ctx;
    ctx.mode = CoordinationMode::Fork;
    ctx.stage_index = 0;
    ctx.total_stages = 4;

    auto result = build_team_coordination(ctx);
    EXPECT_NE(result.find("#1 / 4"), std::string::npos);
    EXPECT_NE(result.find("不要进一步委托"), std::string::npos);
}

TEST(TeamPrompt, NoneReturnsEmpty) {
    TeamContext ctx;
    ctx.mode = CoordinationMode::None;
    auto result = build_team_coordination(ctx);
    EXPECT_TRUE(result.empty());
}

// ─── Budget Awareness ───

TEST(BudgetAwareness, TokensOnly) {
    ResourceBudget budget;
    budget.max_tokens = 100000;

    auto result = build_budget_awareness(budget);
    EXPECT_NE(result.find("100K"), std::string::npos);
}

TEST(BudgetAwareness, DurationMinutes) {
    ResourceBudget budget;
    budget.max_duration_secs = 300;

    auto result = build_budget_awareness(budget);
    EXPECT_NE(result.find("5 分钟"), std::string::npos);
}

TEST(BudgetAwareness, DurationSeconds) {
    ResourceBudget budget;
    budget.max_duration_secs = 30;

    auto result = build_budget_awareness(budget);
    EXPECT_NE(result.find("30 秒"), std::string::npos);
}

TEST(BudgetAwareness, EmptyWhenNoLimits) {
    ResourceBudget budget;
    auto result = build_budget_awareness(budget);
    EXPECT_TRUE(result.empty());
}

TEST(BudgetAwareness, ZeroValuesTreatedAsNone) {
    ResourceBudget budget;
    budget.max_tokens = 0;
    budget.max_duration_secs = 0;
    auto result = build_budget_awareness(budget);
    EXPECT_TRUE(result.empty());
}

// ─── Scene Context ───

TEST(SceneContext, GodContextIncludesForeshadowing) {
    SceneContext ctx;
    ctx.scene_title = "骑士团的秘密";
    ctx.scene_narrative = "深夜的城堡走廊";
    ctx.world_time_label = "第3日 夜";
    ctx.participant_names = {"公主", "骑士团长"};
    ctx.relevant_foreshadowing = {"骑士团长频繁深夜外出（状态：开放）"};
    ctx.known_secrets = {"公主的真实身份 — 知晓者：骑士团长"};

    auto section = build_god_scene_context(ctx);
    EXPECT_NE(section.text.find("骑士团的秘密"), std::string::npos);
    EXPECT_NE(section.text.find("第3日 夜"), std::string::npos);
    EXPECT_NE(section.text.find("伏笔"), std::string::npos);
    EXPECT_NE(section.text.find("秘密"), std::string::npos);
    EXPECT_EQ(section.cache_policy, PromptCachePolicy::None);
}

TEST(SceneContext, CharacterContextIncludesMemories) {
    SceneContext ctx;
    ctx.location = "酒馆";
    ctx.scene_narrative = "傍晚的酒馆里挤满了人";
    ctx.participant_names = {"酒馆老板", "来访者"};
    ctx.recent_memories = {"昨日与来访者发生了争执"};
    ctx.tool_names = {"DescribeCharacter", "SearchMyDiary"};

    auto section = build_character_scene_context(ctx);
    EXPECT_NE(section.text.find("酒馆"), std::string::npos);
    EXPECT_NE(section.text.find("争执"), std::string::npos);
    EXPECT_NE(section.text.find("DescribeCharacter"), std::string::npos);
}

// ─── Compositor Assembly ───

TEST(Compositor, PlatformCoreAssemblesNonEmpty) {
    PromptProfile profile;
    profile.category = AgentCategory::Platform;
    profile.platform_role = PlatformRole::Core;
    profile.memory_mode = MemoryPromptMode::Minimal;
    profile.include_skills = true;

    PromptCompositor compositor;
    std::string result = compositor.assemble(profile);
    EXPECT_FALSE(result.empty());
    EXPECT_NE(result.find("Merak"), std::string::npos);
}

TEST(Compositor, PlatformExploreHasReadOnly) {
    PromptProfile profile;
    profile.category = AgentCategory::Platform;
    profile.platform_role = PlatformRole::Explore;

    PromptCompositor compositor;
    std::string result = compositor.assemble(profile);
    EXPECT_NE(result.find("只读"), std::string::npos);
}

TEST(Compositor, PlatformCodeReviewHasNoStyleComment) {
    PromptProfile profile;
    profile.category = AgentCategory::Platform;
    profile.platform_role = PlatformRole::CodeReview;

    PromptCompositor compositor;
    std::string result = compositor.assemble(profile);
    EXPECT_NE(result.find("不评论代码风格"), std::string::npos);
}

TEST(Compositor, WorldbuildingSceneInjection) {
    PromptProfile profile;
    profile.category = AgentCategory::Worldbuilding;
    profile.worldbuilding_kind = 0; // God — 包含 scene_title

    SceneContext scene_ctx;
    scene_ctx.scene_title = "测试场景";
    scene_ctx.scene_narrative = "测试叙事";
    scene_ctx.world_time_label = "第1日 晨";
    scene_ctx.participant_names = {"角色A"};
    scene_ctx.relevant_foreshadowing = {"一段测试伏笔"};
    profile.scene_ctx = scene_ctx;

    PromptCompositor compositor;
    std::string result = compositor.assemble(profile);
    EXPECT_NE(result.find("测试场景"), std::string::npos);
    EXPECT_NE(result.find("测试伏笔"), std::string::npos);
}

TEST(Compositor, MemoryModeNoneSkipsMemorySection) {
    PromptProfile profile;
    profile.category = AgentCategory::Platform;
    profile.memory_mode = MemoryPromptMode::None;

    PromptCompositor compositor;
    std::string result = compositor.assemble(profile);
    EXPECT_EQ(result.find("Memory 管理规则"), std::string::npos);
}

TEST(Compositor, SkillsDisabledSkipsSkills) {
    PromptProfile profile;
    profile.category = AgentCategory::Platform;
    profile.include_skills = false;

    PromptCompositor compositor;
    std::string result = compositor.assemble(profile);
    EXPECT_EQ(result.find("输出格式"), std::string::npos);
}

TEST(Compositor, TeamCoordinationInjected) {
    PromptProfile profile;
    profile.category = AgentCategory::Platform;

    TeamContext team_ctx;
    team_ctx.mode = CoordinationMode::FanOut;
    team_ctx.agent_id = "agent-a";
    team_ctx.sibling_ids = {"agent-a", "agent-b"};
    team_ctx.aggregation_strategy = "AllResults";
    profile.team_ctx = team_ctx;

    PromptCompositor compositor;
    std::string result = compositor.assemble(profile);
    EXPECT_NE(result.find("团队协作"), std::string::npos);
}

TEST(Compositor, BudgetInjected) {
    PromptProfile profile;
    profile.category = AgentCategory::Platform;

    ResourceBudget budget;
    budget.max_tokens = 50000;
    profile.budget = budget;

    PromptCompositor compositor;
    std::string result = compositor.assemble(profile);
    EXPECT_NE(result.find("50K"), std::string::npos);
}

TEST(Compositor, LayersOrderedGlobalBeforeSessionBeforeNone) {
    PromptProfile profile;
    profile.category = AgentCategory::Platform;

    TeamContext team_ctx;
    team_ctx.mode = CoordinationMode::FanOut;
    team_ctx.agent_id = "a";
    profile.team_ctx = team_ctx;

    PromptCompositor compositor;
    std::string result = compositor.assemble(profile);

    // L1 (Global) 应在 L3 (team coordination, None) 之前
    auto team_pos = result.find("团队协作");
    auto merak_pos = result.find("Merak");
    EXPECT_LT(merak_pos, team_pos);
}
