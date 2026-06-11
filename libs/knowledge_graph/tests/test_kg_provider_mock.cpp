#include <merak/kg/kg_provider.hpp>
#include <gtest/gtest.h>

#include <algorithm>

namespace merak::kg {
namespace {

class MockKGProvider : public KnowledgeGraphProvider {
public:
    void upsert_entity(const GraphEntity& entity) override {
        for (auto& e : entities_) {
            if (e.source_id == entity.source_id && e.world_id == entity.world_id) {
                e = entity;
                return;
            }
        }
        entities_.push_back(entity);
    }

    std::vector<GraphEntity> list_entities(const std::string& world_id) const override {
        std::vector<GraphEntity> result;
        for (auto& e : entities_) {
            if (e.world_id == world_id) result.push_back(e);
        }
        return result;
    }

    void upsert_relation(const GraphRelation& rel) override {
        for (auto& r : relations_) {
            if (r.source_id == rel.source_id && r.target_id == rel.target_id &&
                r.kind_en == rel.kind_en && r.world_id == rel.world_id) {
                r = rel;
                return;
            }
        }
        relations_.push_back(rel);
    }

    void delete_relation(const RelationKey& key) override {
        relations_.erase(
            std::remove_if(relations_.begin(), relations_.end(),
                [&](const GraphRelation& r) {
                    return r.source_id == key.source_id &&
                           r.target_id == key.target_id &&
                           r.kind_en == key.kind_en &&
                           r.world_id == key.world_id;
                }),
            relations_.end());
    }

    SubGraph query_subgraph(const std::string& world_id,
                             const std::vector<std::string>& entity_names,
                             const QueryFilters& filters) const override {
        SubGraph sg;
        for (auto& r : relations_) {
            if (r.world_id != world_id) continue;
            bool src_in = false, tgt_in = false;
            for (auto& n : entity_names) {
                if (n == r.source_name || n == r.source_id) src_in = true;
                if (n == r.target_name || n == r.target_id) tgt_in = true;
            }
            if (src_in && tgt_in) {
                sg.relations.push_back(r);
                sg.fact_summaries.push_back(
                    r.source_name + " → " + r.target_name + ": " + r.kind_cn);
            }
        }
        if (sg.relations.size() > static_cast<size_t>(filters.top_k)) {
            sg.relations.resize(filters.top_k);
            sg.fact_summaries.resize(filters.top_k);
        }
        return sg;
    }

    NeighborGraph expand(const std::string& world_id,
                          const std::string& entity_name,
                          int radius,
                          const QueryFilters& filters) const override {
        NeighborGraph ng;
        ng.center_entity = entity_name;
        if (radius < 1) return ng;

        for (auto& r : relations_) {
            if (r.world_id != world_id) continue;
            if (r.source_name == entity_name || r.source_id == entity_name ||
                r.target_name == entity_name || r.target_id == entity_name) {
                ng.relations.push_back(r);
            }
        }

        for (auto& r : ng.relations) {
            for (auto& e : entities_) {
                if (e.world_id != world_id) continue;
                if (e.name == r.source_name && e.name != entity_name) {
                    if (std::find_if(ng.neighbor_entities.begin(), ng.neighbor_entities.end(),
                            [&](auto& n) { return n.source_id == e.source_id; }) == ng.neighbor_entities.end()) {
                        ng.neighbor_entities.push_back(e);
                    }
                }
                if (e.name == r.target_name && e.name != entity_name) {
                    if (std::find_if(ng.neighbor_entities.begin(), ng.neighbor_entities.end(),
                            [&](auto& n) { return n.source_id == e.source_id; }) == ng.neighbor_entities.end()) {
                        ng.neighbor_entities.push_back(e);
                    }
                }
            }
        }

        return ng;
    }

    PathResult find_paths(const std::string& world_id,
                           const std::string& source,
                           const std::string& target,
                           int max_depth,
                           const QueryFilters& filters) const override {
        PathResult pr;
        pr.found = false;
        for (auto& r : relations_) {
            if (r.world_id != world_id) continue;
            if ((r.source_name == source || r.source_id == source) &&
                (r.target_name == target || r.target_id == target)) {
                pr.paths.push_back({r});
                pr.found = true;
                break;
            }
        }
        return pr;
    }

    void delete_world_graph(const std::string& world_id) override {
        entities_.erase(
            std::remove_if(entities_.begin(), entities_.end(),
                [&](auto& e) { return e.world_id == world_id; }),
            entities_.end());
        relations_.erase(
            std::remove_if(relations_.begin(), relations_.end(),
                [&](auto& r) { return r.world_id == world_id; }),
            relations_.end());
    }

private:
    std::vector<GraphEntity> entities_;
    std::vector<GraphRelation> relations_;
};

// ─── Tests ───

TEST(MockKGProvider, UpsertEntity) {
    MockKGProvider kg;
    GraphEntity e{.name = "艾琳", .type = EntityType::Agent,
                  .source_id = "agent_1", .world_id = "world_1"};
    kg.upsert_entity(e);
    auto list = kg.list_entities("world_1");
    ASSERT_EQ(list.size(), 1);
    ASSERT_EQ(list[0].name, "艾琳");
}

TEST(MockKGProvider, UpsertEntityIdempotent) {
    MockKGProvider kg;
    GraphEntity e{.name = "艾琳", .type = EntityType::Agent,
                  .source_id = "agent_1", .world_id = "world_1"};
    kg.upsert_entity(e);
    e.name = "艾琳·冯";
    kg.upsert_entity(e);
    auto list = kg.list_entities("world_1");
    ASSERT_EQ(list.size(), 1);
    ASSERT_EQ(list[0].name, "艾琳·冯");
}

TEST(MockKGProvider, UpsertRelationIdempotent) {
    MockKGProvider kg;
    GraphRelation r;
    r.source_id = "agent_1"; r.target_id = "agent_2";
    r.source_name = "艾琳"; r.target_name = "卡伦";
    r.kind_en = "master_apprentice"; r.kind_cn = "师徒";
    r.world_id = "world_1";
    kg.upsert_relation(r);

    r.fact = "updated fact";
    kg.upsert_relation(r);

    SubGraph sg = kg.query_subgraph("world_1", {"艾琳", "卡伦"}, {});
    ASSERT_EQ(sg.relations.size(), 1);
    ASSERT_EQ(sg.relations[0].fact, "updated fact");
}

TEST(MockKGProvider, MultipleRelationsBetweenSamePair) {
    MockKGProvider kg;
    GraphRelation r1;
    r1.source_id = "agent_1"; r1.target_id = "agent_2";
    r1.source_name = "艾琳"; r1.target_name = "卡伦";
    r1.kind_en = "master_apprentice"; r1.kind_cn = "师徒";
    r1.world_id = "world_1";
    kg.upsert_relation(r1);

    GraphRelation r2;
    r2.source_id = "agent_1"; r2.target_id = "agent_2";
    r2.source_name = "艾琳"; r2.target_name = "卡伦";
    r2.kind_en = "ally"; r2.kind_cn = "合作/盟友";
    r2.world_id = "world_1";
    kg.upsert_relation(r2);

    SubGraph sg = kg.query_subgraph("world_1", {"艾琳", "卡伦"}, {});
    ASSERT_EQ(sg.relations.size(), 2);
}

TEST(MockKGProvider, QuerySubgraphFiltersByWorld) {
    MockKGProvider kg;
    GraphRelation r1;
    r1.source_id = "a1"; r1.target_id = "a2";
    r1.source_name = "艾琳"; r1.target_name = "卡伦";
    r1.kind_en = "friend"; r1.kind_cn = "朋友";
    r1.world_id = "world_1";
    kg.upsert_relation(r1);

    SubGraph sg = kg.query_subgraph("world_2", {"艾琳", "卡伦"}, {});
    ASSERT_EQ(sg.relations.size(), 0);
}

TEST(MockKGProvider, ExpandRadius) {
    MockKGProvider kg;
    kg.upsert_entity({"艾琳", EntityType::Agent, "a1", "w1"});
    kg.upsert_entity({"卡伦", EntityType::Agent, "a2", "w1"});
    kg.upsert_entity({"玛莎", EntityType::Agent, "a3", "w1"});

    GraphRelation r1;
    r1.source_id = "a1"; r1.target_id = "a2";
    r1.source_name = "艾琳"; r1.target_name = "卡伦";
    r1.kind_en = "friend"; r1.kind_cn = "朋友";
    r1.world_id = "w1";
    kg.upsert_relation(r1);

    GraphRelation r2;
    r2.source_id = "a1"; r2.target_id = "a3";
    r2.source_name = "艾琳"; r2.target_name = "玛莎";
    r2.kind_en = "ally"; r2.kind_cn = "合作/盟友";
    r2.world_id = "w1";
    kg.upsert_relation(r2);

    auto ng = kg.expand("w1", "艾琳", 1, {});
    ASSERT_EQ(ng.relations.size(), 2);
    ASSERT_EQ(ng.neighbor_entities.size(), 2);
}

TEST(MockKGProvider, FindPathDirect) {
    MockKGProvider kg;
    GraphRelation r;
    r.source_id = "a1"; r.target_id = "a2";
    r.source_name = "艾琳"; r.target_name = "卡伦";
    r.kind_en = "friend"; r.kind_cn = "朋友";
    r.world_id = "w1";
    kg.upsert_relation(r);

    auto pr = kg.find_paths("w1", "艾琳", "卡伦", 4, {});
    ASSERT_TRUE(pr.found);
    ASSERT_EQ(pr.paths.size(), 1);
}

TEST(MockKGProvider, FindPathNotFound) {
    MockKGProvider kg;
    auto pr = kg.find_paths("w1", "艾琳", "Nonexistent", 4, {});
    ASSERT_FALSE(pr.found);
}

TEST(MockKGProvider, DeleteWorldGraph) {
    MockKGProvider kg;
    kg.upsert_entity({"艾琳", EntityType::Agent, "a1", "w1"});
    kg.upsert_entity({"卡伦", EntityType::Agent, "a2", "w2"});

    GraphRelation r;
    r.source_id = "a1"; r.target_id = "a2";
    r.source_name = "艾琳"; r.target_name = "卡伦";
    r.kind_en = "friend"; r.kind_cn = "朋友";
    r.world_id = "w1";
    kg.upsert_relation(r);

    kg.delete_world_graph("w1");
    ASSERT_EQ(kg.list_entities("w1").size(), 0);
    ASSERT_EQ(kg.list_entities("w2").size(), 1);
}

} // namespace
} // namespace merak::kg
