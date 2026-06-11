#include <merak/kg/kg_models.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

namespace merak::kg {
namespace {

TEST(KgModels, RelationKindToFromString) {
    EXPECT_EQ(to_string(RelationKind::Friend), "friend");
    EXPECT_EQ(relation_kind_from_string("friend"), RelationKind::Friend);
    EXPECT_EQ(relation_kind_cn(RelationKind::Friend), "朋友");
    EXPECT_EQ(relation_kind_from_string("nonexistent"), RelationKind::Custom);
}

TEST(KgModels, StanceToFromString) {
    EXPECT_EQ(to_string(Stance::Friendly), "Friendly");
    EXPECT_EQ(stance_from_string("Friendly"), Stance::Friendly);
    EXPECT_EQ(to_string(Stance::Hostile), "Hostile");
    EXPECT_EQ(stance_from_string("Hostile"), Stance::Hostile);
    EXPECT_EQ(stance_from_string("Bogus"), Stance::Unknown);
}

TEST(KgModels, EntityTypeToString) {
    EXPECT_EQ(to_string(EntityType::Agent), "Agent");
    EXPECT_EQ(to_string(EntityType::Location), "Location");
    EXPECT_EQ(to_string(EntityType::Organization), "Organization");
}

TEST(KgModels, GraphRelationJsonRoundtrip) {
    GraphRelation r;
    r.source_id = "a1"; r.target_id = "a2";
    r.source_name = "艾琳"; r.target_name = "卡伦";
    r.kind_en = "master_apprentice"; r.kind_cn = "师徒";
    r.a_to_b_stance = Stance::Friendly;
    r.b_to_a_stance = Stance::Hostile;
    r.fact = "艾琳是卡伦的师父";
    r.world_id = "w1";

    nlohmann::json j = r;
    auto r2 = j.get<GraphRelation>();

    EXPECT_EQ(r2.source_id, r.source_id);
    EXPECT_EQ(r2.target_id, r.target_id);
    EXPECT_EQ(r2.kind_en, r.kind_en);
    EXPECT_EQ(r2.a_to_b_stance, r.a_to_b_stance);
    EXPECT_EQ(r2.b_to_a_stance, r.b_to_a_stance);
    EXPECT_EQ(r2.fact, r.fact);
    EXPECT_EQ(r2.world_id, r.world_id);
}

} // namespace
} // namespace merak::kg
