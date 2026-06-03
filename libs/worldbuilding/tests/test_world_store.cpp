#include <gtest/gtest.h>
#include <merak/worldbuilding/ids.hpp>
#include <merak/worldbuilding/world_models.hpp>

using namespace merak::worldbuilding;

TEST(WorldbuildingModels, RequiredDefaultsMatchDesign) {
    Chapter chapter;
    EXPECT_EQ(chapter.status, ChapterStatus::Outline);
    EXPECT_TRUE(chapter.scene_ids.empty());

    Foreshadowing f;
    EXPECT_EQ(f.status, ForeshadowStatus::Open);
    EXPECT_EQ(f.created_by, ForeshadowCreatedBy::Author);

    Secret s;
    EXPECT_EQ(s.status, SecretStatus::Active);

    CharacterCard card;
    EXPECT_EQ(card.version, 1);
    EXPECT_TRUE(card.taboo_topics.empty());
}

TEST(WorldbuildingIds, PrefixesAreStableAndNonEmpty) {
    EXPECT_TRUE(make_id("world").starts_with("world_"));
    EXPECT_TRUE(make_id("agent").starts_with("agent_"));
    EXPECT_NE(make_id("scene"), make_id("scene"));
}
