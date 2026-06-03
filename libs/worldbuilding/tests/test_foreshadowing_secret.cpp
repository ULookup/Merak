#include <gtest/gtest.h>
#include <merak/worldbuilding/foreshadowing_store.hpp>
#include <merak/worldbuilding/ids.hpp>
#include <merak/worldbuilding/narrative_store.hpp>
#include <merak/worldbuilding/secret_store.hpp>
#include <merak/worldbuilding/world_store.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace merak::worldbuilding;

namespace {

std::filesystem::path temp_dir() {
    auto path = std::filesystem::temp_directory_path() / make_id("fs_test");
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

std::string slurp(const std::filesystem::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

struct Fixture {
    std::filesystem::path root = temp_dir();
    WorldStore worlds{root};
    WorldMeta world;
    NarrativeStore narrative;
    ForeshadowingStore foreshadowing;

    Fixture()
        : world(worlds.create_world("北境", "雪原史诗")),
          narrative(worlds, root),
          foreshadowing(worlds, narrative, root) {}
};

} // namespace

// ── Foreshadowing tests ──────────────────────────────────────────────

TEST(ForeshadowingStore, PlantStoresOpenItemWithMetadata) {
    Fixture f;

    Foreshadowing item;
    item.content = "铁匠卡伦右手缺一根手指";
    item.pay_off_idea = "卡伦是退役佣兵，断指烙着旧主纹章，后期将用它指认凶手";
    item.hint_level = ForeshadowHintLevel::Subtle;
    item.tags = {"agent_kalun", "tavern"};
    item.related_secret_ids = {"secret_old_lord"};
    auto planted = f.foreshadowing.plant(f.world.id, item);

    EXPECT_FALSE(planted.id.empty());
    EXPECT_EQ(planted.status, ForeshadowStatus::Open);
    EXPECT_EQ(planted.hint_level, ForeshadowHintLevel::Subtle);
    EXPECT_EQ(planted.tags, std::vector<std::string>({"agent_kalun", "tavern"}));
    EXPECT_EQ(planted.related_secret_ids, std::vector<std::string>({"secret_old_lord"}));

    const auto path = f.worlds.world_path(f.world.id) / "foreshadows" / (planted.id + ".json");
    ASSERT_TRUE(std::filesystem::exists(path));
    auto json = nlohmann::json::parse(slurp(path));
    EXPECT_EQ(json["status"], "open");
    EXPECT_EQ(json["content"], "铁匠卡伦右手缺一根手指");
}

TEST(ForeshadowingStore, RelevantForSceneMatchesByParticipantAndLocationTags) {
    Fixture f;

    Foreshadowing a;
    a.content = "林霜的项链";
    a.tags = {"agent_linshi"};
    a = f.foreshadowing.plant(f.world.id, a);

    Foreshadowing b;
    b.content = "旅店暗门";
    b.tags = {"tavern", "inn_secret"};
    b = f.foreshadowing.plant(f.world.id, b);

    Foreshadowing c;
    c.content = "无关伏笔";
    c.tags = {"castle"};
    c = f.foreshadowing.plant(f.world.id, c);

    Scene scene;
    scene.participant_ids = {"agent_linshi", "agent_masha"};
    scene.location_id = "inn_secret";

    auto relevant = f.foreshadowing.relevant_for_scene(f.world.id, scene);
    ASSERT_EQ(relevant.size(), 2);
    auto ids = std::vector<std::string>{relevant[0].id, relevant[1].id};
    EXPECT_NE(std::find(ids.begin(), ids.end(), a.id), ids.end());
    EXPECT_NE(std::find(ids.begin(), ids.end(), b.id), ids.end());
    EXPECT_EQ(std::find(ids.begin(), ids.end(), c.id), ids.end());
}

TEST(ForeshadowingStore, PayMarksPaidAtAndMovesChapterLinkage) {
    Fixture f;

    Chapter chapter;
    chapter.title = "雪夜";
    chapter.number = 1;
    auto created_chapter = f.narrative.create_chapter(f.world.id, chapter);

    Foreshadowing item;
    item.content = "狼烟令烧焦一角";
    item.pay_off_idea = "烧焦痕迹来自王都魔法";
    auto planted = f.foreshadowing.plant(f.world.id, item);

    // Manually add to chapter planted list
    auto chapter_path = f.worlds.world_path(f.world.id) / "chapters" / (created_chapter.id + ".json");
    auto chapter_json = nlohmann::json::parse(slurp(chapter_path));
    chapter_json["foreshadowing_planted"].push_back(planted.id);
    {
        std::ofstream out(chapter_path);
        out << chapter_json.dump(2);
    }

    Scene scene;
    scene.title = "旅店试探";
    scene.chapter_id = created_chapter.id;
    scene.world_time = "第二日夜";
    scene.participant_ids = {"agent_linshi"};
    auto paid_scene = f.narrative.create_scene(f.world.id, scene);

    auto paid = f.foreshadowing.pay(f.world.id, planted.id, paid_scene.id);
    EXPECT_EQ(paid.status, ForeshadowStatus::Paid);
    EXPECT_TRUE(paid.paid_at.has_value());
    EXPECT_EQ(*paid.paid_at, paid_scene.id);

    // Chapter linkage should have moved from planted to paid
    auto updated_json = nlohmann::json::parse(slurp(chapter_path));
    auto planted_vec = updated_json["foreshadowing_planted"].get<std::vector<std::string>>();
    auto paid_vec = updated_json["foreshadowing_paid"].get<std::vector<std::string>>();
    EXPECT_EQ(std::find(planted_vec.begin(), planted_vec.end(), planted.id), planted_vec.end());
    EXPECT_NE(std::find(paid_vec.begin(), paid_vec.end(), planted.id), paid_vec.end());
}

TEST(ForeshadowingStore, AbandonKeepsRecordAndSuppressesReminders) {
    Fixture f;

    Foreshadowing item;
    item.content = "废弃线索";
    auto planted = f.foreshadowing.plant(f.world.id, item);

    auto abandoned = f.foreshadowing.abandon(f.world.id, planted.id);
    EXPECT_EQ(abandoned.status, ForeshadowStatus::Abandoned);

    // Should not appear in final act reminders
    auto reminders = f.foreshadowing.final_act_reminders(f.world.id);
    EXPECT_TRUE(reminders.empty());

    // Should appear in list when filtering by Abandoned
    auto list = f.foreshadowing.list(f.world.id, ForeshadowStatus::Abandoned);
    ASSERT_EQ(list.size(), 1);
    EXPECT_EQ(list[0].id, planted.id);
}

TEST(ForeshadowingStore, StatsCountsByStatus) {
    Fixture f;

    Foreshadowing a;
    a.content = "open item";
    f.foreshadowing.plant(f.world.id, a);

    Foreshadowing b;
    b.content = "paid item";
    b = f.foreshadowing.plant(f.world.id, b);

    Chapter ch;
    ch.title = "test";
    ch.number = 1;
    auto chapter = f.narrative.create_chapter(f.world.id, ch);

    Scene sc;
    sc.title = "test scene";
    sc.chapter_id = chapter.id;
    sc.world_time = "day 1";
    sc.participant_ids = {"agent_x"};
    auto scene = f.narrative.create_scene(f.world.id, sc);

    auto paid = f.foreshadowing.pay(f.world.id, b.id, scene.id);

    Foreshadowing c;
    c.content = "abandoned item";
    c = f.foreshadowing.plant(f.world.id, c);
    f.foreshadowing.abandon(f.world.id, c.id);

    auto s = f.foreshadowing.stats(f.world.id);
    EXPECT_EQ(s.open, 1);
    EXPECT_EQ(s.paid, 1);
    EXPECT_EQ(s.abandoned, 1);
}

TEST(ForeshadowingStore, ChapterSummaryReturnsPlantedPaidOpenCounts) {
    Fixture f;

    Chapter ch;
    ch.title = "summary chapter";
    ch.number = 1;
    auto chapter = f.narrative.create_chapter(f.world.id, ch);

    Foreshadowing item;
    item.content = "test item";
    auto planted = f.foreshadowing.plant(f.world.id, item);

    Scene sc;
    sc.title = "pay scene";
    sc.chapter_id = chapter.id;
    sc.world_time = "day 1";
    sc.participant_ids = {"agent_x"};
    auto scene = f.narrative.create_scene(f.world.id, sc);

    auto chapter_path = f.worlds.world_path(f.world.id) / "chapters" / (chapter.id + ".json");
    auto chapter_json = nlohmann::json::parse(slurp(chapter_path));
    chapter_json["foreshadowing_planted"].push_back(planted.id);
    {
        std::ofstream out(chapter_path);
        out << chapter_json.dump(2);
    }

    f.foreshadowing.pay(f.world.id, planted.id, scene.id);

    auto summary = f.foreshadowing.chapter_summary(f.world.id, chapter.id);
    EXPECT_EQ(summary.paid, 1);
    EXPECT_EQ(summary.open, 0);
}

TEST(ForeshadowingStore, FinalActRemindersReturnsOpenItemsOnlyInFinalStage) {
    Fixture f;

    f.narrative.create_story_structure(f.world.id, NarrativeTemplate::ThreeAct);

    Foreshadowing item;
    item.content = "must resolve";
    f.foreshadowing.plant(f.world.id, item);

    // The default story structure has stages ["建立", "对抗", "解决"]
    // The last stage is "解决", so final_act_reminders should return open items
    auto reminders = f.foreshadowing.final_act_reminders(f.world.id);
    ASSERT_EQ(reminders.size(), 1);
    EXPECT_EQ(reminders[0].content, "must resolve");
}

// ── Secret tests ─────────────────────────────────────────────────────

TEST(SecretStore, CreateStoresKnowledgeBarriers) {
    auto root = temp_dir();
    WorldStore worlds(root);
    auto world = worlds.create_world("北境", "雪原史诗");
    NarrativeStore narrative(worlds, root);
    ForeshadowingStore foreshadowing(worlds, narrative, root);
    SecretStore secrets(worlds, foreshadowing, root);

    Secret secret;
    secret.holder_id = "agent_ailin";
    secret.truth = "艾琳真实身份是王都骑士团教官";
    secret.public_version = "艾琳自称是从王都逃难的旅人";
    secret.stakes = "身份暴露将引来追杀";
    secret.aware_character_ids = {"agent_masha"};
    secret.suspicious_character_ids = {"agent_kalun"};
    secret.believed_truths = nlohmann::json::object({{"agent_kalun", "艾琳可能是逃兵"}});

    auto created = secrets.create(world.id, secret);

    EXPECT_FALSE(created.id.empty());
    EXPECT_EQ(created.status, SecretStatus::Active);
    EXPECT_EQ(created.holder_id, "agent_ailin");
    EXPECT_EQ(created.truth, "艾琳真实身份是王都骑士团教官");
    EXPECT_EQ(created.public_version, "艾琳自称是从王都逃难的旅人");
}

TEST(SecretStore, SceneAsymmetryFiltersPerCharacterKnowledge) {
    auto root = temp_dir();
    WorldStore worlds(root);
    auto world = worlds.create_world("北境", "雪原史诗");
    NarrativeStore narrative(worlds, root);
    ForeshadowingStore foreshadowing(worlds, narrative, root);
    SecretStore secrets(worlds, foreshadowing, root);

    Secret secret;
    secret.holder_id = "agent_ailin";
    secret.truth = "艾琳是骑士团教官";
    secret.public_version = "艾琳是普通旅人";
    secret.stakes = "追杀";
    secret.aware_character_ids = {"agent_masha"};
    secret.suspicious_character_ids = {"agent_kalun"};
    secret.believed_truths = nlohmann::json::object({{"agent_kalun", "艾琳是逃兵"}});
    secrets.create(world.id, secret);

    Chapter ch;
    ch.title = "test";
    ch.number = 1;
    auto chapter = narrative.create_chapter(world.id, ch);

    Scene scene;
    scene.title = "旅店";
    scene.chapter_id = chapter.id;
    scene.world_time = "day 1";
    scene.participant_ids = {"agent_ailin", "agent_masha", "agent_kalun", "agent_stranger"};
    auto created_scene = narrative.create_scene(world.id, scene);

    auto views = secrets.scene_asymmetry(world.id, created_scene);

    ASSERT_EQ(views.size(), 4);

    for (const auto& view : views) {
        if (view.character_id == "agent_ailin") {
            // Holder knows truth
            EXPECT_EQ(view.state, KnowledgeState::Secret);
            EXPECT_NE(view.context_snippet.find("骑士团教官"), std::string::npos);
        } else if (view.character_id == "agent_masha") {
            // Aware character knows truth
            EXPECT_EQ(view.state, KnowledgeState::Secret);
            EXPECT_NE(view.context_snippet.find("骑士团教官"), std::string::npos);
        } else if (view.character_id == "agent_kalun") {
            // Suspicious character gets believed truth
            EXPECT_NE(view.context_snippet.find("逃兵"), std::string::npos);
        } else if (view.character_id == "agent_stranger") {
            // Unknown character gets public version
            EXPECT_EQ(view.state, KnowledgeState::Public);
            EXPECT_NE(view.context_snippet.find("普通旅人"), std::string::npos);
        }
    }
}

TEST(SecretStore, CheckLeakRiskFlagsTruthInWrongContext) {
    auto root = temp_dir();
    WorldStore worlds(root);
    auto world = worlds.create_world("北境", "雪原史诗");
    NarrativeStore narrative(worlds, root);
    ForeshadowingStore foreshadowing(worlds, narrative, root);
    SecretStore secrets(worlds, foreshadowing, root);

    Secret secret;
    secret.holder_id = "agent_ailin";
    secret.truth = "艾琳是骑士团教官";
    secret.public_version = "艾琳是普通旅人";
    secret.stakes = "追杀";
    secrets.create(world.id, secret);

    Chapter ch;
    ch.title = "test";
    ch.number = 1;
    auto chapter = narrative.create_chapter(world.id, ch);

    Scene scene;
    scene.title = "test";
    scene.chapter_id = chapter.id;
    scene.world_time = "day 1";
    scene.participant_ids = {"agent_stranger"};
    auto created_scene = narrative.create_scene(world.id, scene);

    // Stranger shouldn't know the truth, but the draft mentions it
    auto risks = secrets.check_leak_risk(world.id, created_scene, "艾琳说她是骑士团教官");
    ASSERT_EQ(risks.size(), 1);
    EXPECT_NE(risks[0].reason.find("骑士团教官"), std::string::npos);
    EXPECT_EQ(risks[0].character_id, "agent_stranger");
}

TEST(SecretStore, ExposeChangesStatusAndPaysRelatedForeshadowing) {
    auto root = temp_dir();
    WorldStore worlds(root);
    auto world = worlds.create_world("北境", "雪原史诗");
    NarrativeStore narrative(worlds, root);
    ForeshadowingStore foreshadowing(worlds, narrative, root);
    SecretStore secrets(worlds, foreshadowing, root);

    Foreshadowing fs;
    fs.content = "艾琳的剑法不像是平民";
    fs.pay_off_idea = "剑法是骑士训练";
    auto planted_fs = foreshadowing.plant(world.id, fs);

    Secret secret;
    secret.holder_id = "agent_ailin";
    secret.truth = "艾琳是骑士团教官";
    secret.public_version = "普通旅人";
    secret.stakes = "追杀";
    secret.related_foreshadowing_ids = {planted_fs.id};
    auto created = secrets.create(world.id, secret);

    Chapter ch;
    ch.title = "test";
    ch.number = 1;
    auto chapter = narrative.create_chapter(world.id, ch);

    Scene scene;
    scene.title = "reveal";
    scene.chapter_id = chapter.id;
    scene.world_time = "day 5";
    scene.participant_ids = {"agent_ailin", "agent_masha"};
    auto created_scene = narrative.create_scene(world.id, scene);

    auto exposed = secrets.expose(world.id, created.id, created_scene.id);
    EXPECT_EQ(exposed.status, SecretStatus::Exposed);
    EXPECT_TRUE(exposed.exposed_at.has_value());
    EXPECT_EQ(*exposed.exposed_at, created_scene.id);

    // Related foreshadowing should be paid
    auto updated_fs = foreshadowing.list(world.id, ForeshadowStatus::Paid);
    ASSERT_EQ(updated_fs.size(), 1);
    EXPECT_EQ(updated_fs[0].id, planted_fs.id);
}

TEST(SecretStore, TransferAddsAwareCharacterWithoutExposing) {
    auto root = temp_dir();
    WorldStore worlds(root);
    auto world = worlds.create_world("北境", "雪原史诗");
    NarrativeStore narrative(worlds, root);
    ForeshadowingStore foreshadowing(worlds, narrative, root);
    SecretStore secrets(worlds, foreshadowing, root);

    Secret secret;
    secret.holder_id = "agent_ailin";
    secret.truth = "艾琳是骑士团教官";
    secret.public_version = "普通旅人";
    secret.stakes = "追杀";
    secret.aware_character_ids = {"agent_masha"};
    auto created = secrets.create(world.id, secret);

    auto transferred = secrets.transfer(world.id, created.id, "agent_road");
    EXPECT_EQ(transferred.status, SecretStatus::Active);
    EXPECT_EQ(transferred.aware_character_ids.size(), 2);
    EXPECT_NE(std::find(transferred.aware_character_ids.begin(),
                         transferred.aware_character_ids.end(), "agent_road"),
              transferred.aware_character_ids.end());
}

TEST(SecretStore, ReverseTruthArchivesOldAndCreatesDeeper) {
    auto root = temp_dir();
    WorldStore worlds(root);
    auto world = worlds.create_world("北境", "雪原史诗");
    NarrativeStore narrative(worlds, root);
    ForeshadowingStore foreshadowing(worlds, narrative, root);
    SecretStore secrets(worlds, foreshadowing, root);

    Secret secret;
    secret.holder_id = "agent_ailin";
    secret.truth = "艾琳是骑士团教官";
    secret.public_version = "普通旅人";
    secret.stakes = "追杀";
    auto created = secrets.create(world.id, secret);

    auto reversed = secrets.reverse_truth(world.id, created.id,
                                           "艾琳曾是先王的贴身护卫",
                                           "新王登基后清洗先王旧部");
    EXPECT_EQ(reversed.status, SecretStatus::Active);
    EXPECT_EQ(reversed.truth, "艾琳曾是先王的贴身护卫");
    EXPECT_EQ(reversed.stakes, "新王登基后清洗先王旧部");
    EXPECT_NE(reversed.id, created.id);
}

TEST(SecretStore, ListFiltersByStatus) {
    auto root = temp_dir();
    WorldStore worlds(root);
    auto world = worlds.create_world("北境", "雪原史诗");
    NarrativeStore narrative(worlds, root);
    ForeshadowingStore foreshadowing(worlds, narrative, root);
    SecretStore secrets(worlds, foreshadowing, root);

    Secret a;
    a.holder_id = "agent_ailin";
    a.truth = "secret a";
    a.public_version = "public a";
    a.stakes = "stakes";
    secrets.create(world.id, a);

    Secret b;
    b.holder_id = "agent_masha";
    b.truth = "secret b";
    b.public_version = "public b";
    b.stakes = "stakes";
    auto created_b = secrets.create(world.id, b);
    secrets.abandon(world.id, created_b.id);

    auto active = secrets.list(world.id, SecretStatus::Active);
    auto abandoned = secrets.list(world.id, SecretStatus::Abandoned);
    auto all = secrets.list(world.id, std::nullopt);

    EXPECT_EQ(active.size(), 1);
    EXPECT_EQ(abandoned.size(), 1);
    EXPECT_EQ(all.size(), 2);
}
