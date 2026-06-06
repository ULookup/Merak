#include <gtest/gtest.h>
#include <merak/worldbuilding/ids.hpp>
#include <merak/worldbuilding/worldbuilding_service.hpp>

#include "test_helpers.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace merak::worldbuilding;
using namespace merak::worldbuilding::test;

namespace {

std::filesystem::path temp_dir() {
    auto path = std::filesystem::temp_directory_path() / make_id("creation_test");
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

nlohmann::json slurp_json(const std::filesystem::path& path) {
    std::ifstream input(path);
    return nlohmann::json::parse(input);
}

} // namespace

class CreationFlowTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = temp_dir();
        service_ = std::make_unique<WorldbuildingService>(test_pg_conninfo(), root_);
        service_->initialize();

        auto world = service_->create_world("测试世界", "用于创建流程单元测试");
        world_id_ = world.id;

        // Create a couple of characters for scene and secret tests
        CharacterCard card_a;
        card_a.name = "测试角色A";
        card_a.gender = "男";
        card_a.age = 25;
        card_a.identity = "战士";
        card_a.emotional_tendency = "冷静";
        card_a.speaking_style = "简短";
        card_a.core_desire = "守护";
        card_a.deep_fear = "失败";
        card_a.appearance = "高大";
        card_a.background = "军人";
        card_a.knowledge_scope = "战斗";
        card_a.core_traits = {"勇敢"};
        char_a_ = service_->create_character(world_id_, card_a);

        CharacterCard card_b;
        card_b.name = "测试角色B";
        card_b.gender = "女";
        card_b.age = 22;
        card_b.identity = "法师";
        card_b.emotional_tendency = "热情";
        card_b.speaking_style = "话多";
        card_b.core_desire = "知识";
        card_b.deep_fear = "无知";
        card_b.appearance = "娇小";
        card_b.background = "学者";
        card_b.knowledge_scope = "魔法";
        card_b.core_traits = {"聪明"};
        char_b_ = service_->create_character(world_id_, card_b);

        // Create a chapter for scene resolution tests
        Chapter chap;
        chap.title = "测试章节";
        chap.number = 1;
        chap.pitch = "章节概要";
        chapter_ = service_->create_chapter(world_id_, chap);

        // Create an arc for reference in chapter preview tests
        Arc arc;
        arc.title = "测试弧线";
        arc.purpose = "测试目的";
        arc_ = service_->create_arc(world_id_, arc);
    }

    void TearDown() override {
        service_.reset();
        if (!root_.empty()) {
            std::filesystem::remove_all(root_);
        }
    }

    std::filesystem::path root_;
    std::unique_ptr<WorldbuildingService> service_;
    std::string world_id_;
    AgentRecord char_a_;
    AgentRecord char_b_;
    Chapter chapter_;
    Arc arc_;
};

// ── Preview builder tests ────────────────────────────────────────────────────

TEST_F(CreationFlowTest, BuildScenePreviewReturnsCorrectStructure) {
    auto preview = service_->build_scene_preview(world_id_, {
        {"title", "雪夜来客"},
        {"chapter_id", chapter_.id},
        {"world_time", "第三日夜"},
        {"narrative", "风暴前夕"},
        {"participant_ids", {char_a_.id, char_b_.id}},
        {"location_id", "wolf_inn"}
    });

    EXPECT_EQ(preview["title"], "雪夜来客");
    EXPECT_EQ(preview["chapter_id"], chapter_.id);
    EXPECT_EQ(preview["world_time"], "第三日夜");
    EXPECT_EQ(preview["narrative"], "风暴前夕");
    EXPECT_EQ(preview["location_id"], "wolf_inn");
    ASSERT_TRUE(preview["participant_ids"].is_array());
    EXPECT_EQ(preview["participant_ids"].size(), 2);
    EXPECT_EQ(preview["participant_ids"][0], char_a_.id);
    EXPECT_EQ(preview["participant_ids"][1], char_b_.id);
}

TEST_F(CreationFlowTest, BuildChapterPreviewMapsOrderIndexToNumber) {
    auto preview = service_->build_chapter_preview(world_id_, {
        {"title", "第二章"},
        {"order_index", 3},
        {"summary", "主角踏上征途"},
        {"arc_id", arc_.id}
    });

    EXPECT_EQ(preview["title"], "第二章");
    EXPECT_EQ(preview["number"], 3);
    EXPECT_EQ(preview["pitch"], "主角踏上征途");
    EXPECT_EQ(preview["arc_id"], arc_.id);
}

TEST_F(CreationFlowTest, BuildArcPreviewMapsNameToTitle) {
    auto preview = service_->build_arc_preview(world_id_, {
        {"name", "复仇弧线"},
        {"description", "主角走上复仇之路"},
        {"chapter_numbers", {1, 2, 3}}
    });

    EXPECT_EQ(preview["title"], "复仇弧线");
    EXPECT_EQ(preview["purpose"], "主角走上复仇之路");
    ASSERT_TRUE(preview["chapter_numbers"].is_array());
    EXPECT_EQ(preview["chapter_numbers"].size(), 3);
}

TEST_F(CreationFlowTest, BuildSecretPreviewUsesHolderId) {
    auto preview = service_->build_secret_preview(world_id_, {
        {"truth", "真实身份是王室后裔"},
        {"holder_id", char_a_.id},
        {"stakes", "身份暴露将引来追杀令"}
    });

    EXPECT_EQ(preview["truth"], "真实身份是王室后裔");
    EXPECT_EQ(preview["holder_id"], char_a_.id);
    EXPECT_EQ(preview["stakes"], "身份暴露将引来追杀令");
}

TEST_F(CreationFlowTest, BuildSecretPreviewFallsBackToHolderAgentIds) {
    auto preview = service_->build_secret_preview(world_id_, {
        {"truth", "持有者通过数组传入"},
        {"holder_agent_ids", {char_a_.id, char_b_.id}},
        {"stakes", "非常重要"}
    });

    EXPECT_EQ(preview["truth"], "持有者通过数组传入");
    EXPECT_EQ(preview["holder_id"], char_a_.id);
    EXPECT_EQ(preview["stakes"], "非常重要");
}

TEST_F(CreationFlowTest, BuildWorldKnowledgePreview) {
    auto preview = service_->build_world_knowledge_preview(world_id_, {
        {"category", "history"},
        {"content", "千年前的大战毁灭了古王国"},
        {"tags", {"战争", "古代", "王国"}},
        {"related_ids", {"rel_1", "rel_2"}}
    });

    EXPECT_EQ(preview["category"], "history");
    EXPECT_EQ(preview["content"], "千年前的大战毁灭了古王国");
    ASSERT_TRUE(preview["tags"].is_array());
    EXPECT_EQ(preview["tags"].size(), 3);
    EXPECT_EQ(preview["tags"][0], "战争");
    ASSERT_TRUE(preview["related_ids"].is_array());
    EXPECT_EQ(preview["related_ids"].size(), 2);
}

TEST_F(CreationFlowTest, BuildLocationPreview) {
    auto preview = service_->build_location_preview(world_id_, {
        {"name", "狼烟旅店"},
        {"description", "北境最古老的旅店"},
        {"region", "北境"}
    });

    EXPECT_EQ(preview["name"], "狼烟旅店");
    EXPECT_EQ(preview["description"], "北境最古老的旅店");
    EXPECT_EQ(preview["region"], "北境");
}

// ── Store/retrieve tests ──────────────────────────────────────────────────────

TEST_F(CreationFlowTest, StoreAndRetrievePendingCreation) {
    auto creation_id = service_->store_pending_creation(
        world_id_, "create_chapter",
        {{"title", "测试待定章节"}, {"number", 5}},
        {{"title", "测试待定章节"}, {"number", 5}}
    );

    EXPECT_FALSE(creation_id.empty());
    EXPECT_NE(creation_id.find("creation_"), std::string::npos);

    auto opt = service_->get_pending_creation(creation_id);
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(opt->creation_id, creation_id);
    EXPECT_EQ(opt->tool_name, "create_chapter");
    EXPECT_EQ(opt->world_id, world_id_);
    EXPECT_EQ(opt->params["title"], "测试待定章节");
    EXPECT_EQ(opt->params["number"], 5);
}

TEST_F(CreationFlowTest, GetPendingCreationReturnsNulloptForUnknown) {
    auto opt = service_->get_pending_creation("bogus_creation_id_does_not_exist");
    EXPECT_FALSE(opt.has_value());
}

// ── Resolution tests ──────────────────────────────────────────────────────────

TEST_F(CreationFlowTest, ResolveCreationDenyRemovesPending) {
    auto creation_id = service_->store_pending_creation(
        world_id_, "create_chapter",
        {{"title", "待删除章节"}},
        {{"title", "待删除章节"}}
    );

    auto result = service_->resolve_creation(creation_id, "deny", {});
    EXPECT_EQ(result["decision"], "deny");
    EXPECT_EQ(result["creation_id"], creation_id);

    auto opt = service_->get_pending_creation(creation_id);
    EXPECT_FALSE(opt.has_value());
}

TEST_F(CreationFlowTest, ResolveCreationModifyMergesChanges) {
    auto creation_id = service_->store_pending_creation(
        world_id_, "create_chapter",
        {{"title", "原始章节名称"}},
        {{"title", "原始章节名称"}}
    );

    auto result = service_->resolve_creation(creation_id, "modify",
        {{"title", "修改后的章节名称"}});

    EXPECT_EQ(result["decision"], "modify");
    EXPECT_EQ(result["creation_id"], creation_id);
    EXPECT_TRUE(result.contains("chapter_id"));

    // Pending creation should be removed after resolution
    auto opt = service_->get_pending_creation(creation_id);
    EXPECT_FALSE(opt.has_value());

    // Verify the chapter JSON on disk has the modified title
    auto chapter_path = service_->worlds().world_path(world_id_) /
                        "chapters" / (result["chapter_id"].get<std::string>() + ".json");
    ASSERT_TRUE(std::filesystem::exists(chapter_path));
    auto chapter_json = slurp_json(chapter_path);
    EXPECT_EQ(chapter_json["title"], "修改后的章节名称");
}

TEST_F(CreationFlowTest, ResolveCreationForEachToolType) {
    // Chapter
    {
        auto cid = service_->store_pending_creation(
            world_id_, "create_chapter",
            {{"title", "全新章节"}, {"number", 42}, {"pitch", "节节攀升"}},
            {{"title", "全新章节"}, {"number", 42}}
        );

        auto result = service_->resolve_creation(cid, "allow", {});
        EXPECT_EQ(result["decision"], "allow");
        EXPECT_TRUE(result.contains("chapter_id"));

        auto chapter_path = service_->worlds().world_path(world_id_) /
                            "chapters" / (result["chapter_id"].get<std::string>() + ".json");
        ASSERT_TRUE(std::filesystem::exists(chapter_path));
        auto json = slurp_json(chapter_path);
        EXPECT_EQ(json["title"], "全新章节");
        EXPECT_EQ(json["number"], 42);
    }

    // Arc
    {
        auto cid = service_->store_pending_creation(
            world_id_, "create_arc",
            {{"title", "全新弧线"}, {"purpose", "探索未知"}, {"chapter_numbers", {10, 11}}},
            {{"title", "全新弧线"}}
        );

        auto result = service_->resolve_creation(cid, "allow", {});
        EXPECT_EQ(result["decision"], "allow");
        EXPECT_TRUE(result.contains("arc_id"));

        auto arc_path = service_->worlds().world_path(world_id_) /
                        "arcs" / (result["arc_id"].get<std::string>() + ".json");
        ASSERT_TRUE(std::filesystem::exists(arc_path));
        auto json = slurp_json(arc_path);
        EXPECT_EQ(json["title"], "全新弧线");
        EXPECT_EQ(json["purpose"], "探索未知");
    }

    // Secret
    {
        auto cid = service_->store_pending_creation(
            world_id_, "create_secret",
            {{"truth", "隐藏的真相"}, {"holder_id", char_a_.id}, {"stakes", "世界毁灭"}},
            {{"truth", "隐藏的真相"}}
        );

        auto result = service_->resolve_creation(cid, "allow", {});
        EXPECT_EQ(result["decision"], "allow");
        EXPECT_TRUE(result.contains("secret_id"));

        auto secret_path = service_->worlds().world_path(world_id_) /
                           "secrets" / (result["secret_id"].get<std::string>() + ".json");
        ASSERT_TRUE(std::filesystem::exists(secret_path));
        auto json = slurp_json(secret_path);
        EXPECT_EQ(json["truth"], "隐藏的真相");
        EXPECT_EQ(json["holder_id"], char_a_.id);
        EXPECT_EQ(json["stakes"], "世界毁灭");
    }

    // World knowledge
    {
        auto cid = service_->store_pending_creation(
            world_id_, "add_world_knowledge",
            {{"category", "map"}, {"content", "北方是冰原"}, {"tags", {"地理", "气候"}}},
            {{"category", "map"}}
        );

        auto result = service_->resolve_creation(cid, "allow", {});
        EXPECT_EQ(result["decision"], "allow");
        EXPECT_TRUE(result.contains("knowledge_id"));

        // Verify using get_world_knowledge
        auto knowledge = service_->worlds().get_world_knowledge(world_id_, "map");
        bool found = false;
        for (auto& kw : knowledge) {
            if (kw.id == result["knowledge_id"].get<std::string>()) {
                found = true;
                EXPECT_EQ(kw.content, "北方是冰原");
                EXPECT_EQ(kw.category, "map");
                break;
            }
        }
        EXPECT_TRUE(found);
    }

    // Location
    {
        auto cid = service_->store_pending_creation(
            world_id_, "create_location",
            {{"name", "冰封要塞"}, {"description", "北境防线"}, {"region", "极北"}},
            {{"name", "冰封要塞"}}
        );

        auto result = service_->resolve_creation(cid, "allow", {});
        EXPECT_EQ(result["decision"], "allow");
        EXPECT_TRUE(result.contains("location_id"));

        auto loc = service_->worlds().get_location(world_id_,
                                                    result["location_id"].get<std::string>());
        ASSERT_TRUE(loc.has_value());
        EXPECT_EQ(loc->name, "冰封要塞");
        EXPECT_EQ(loc->region, "极北");
    }

    // Scene (needs a valid chapter_id)
    {
        auto cid = service_->store_pending_creation(
            world_id_, "create_scene",
            {
                {"title", "要塞会面"},
                {"chapter_id", chapter_.id},
                {"world_time", "第五日昼"},
                {"participant_ids", {char_a_.id, char_b_.id}},
                {"location_id", "fortress_gate"}
            },
            {{"title", "要塞会面"}}
        );

        auto result = service_->resolve_creation(cid, "allow", {});
        EXPECT_EQ(result["decision"], "allow");
        EXPECT_TRUE(result.contains("scene_id"));

        auto sc = service_->narrative().get_scene(world_id_,
                                                   result["scene_id"].get<std::string>());
        ASSERT_TRUE(sc.has_value());
        EXPECT_EQ(sc->title, "要塞会面");
        EXPECT_EQ(sc->chapter_id, chapter_.id);
    }
}
