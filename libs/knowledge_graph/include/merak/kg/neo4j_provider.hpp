#pragma once

#include <merak/kg/kg_provider.hpp>

#include <memory>
#include <string>

namespace merak::kg {

class Neo4jKGProvider : public KnowledgeGraphProvider {
public:
    Neo4jKGProvider(std::string uri, std::string user, std::string password,
                    std::string database = "merak");
    ~Neo4jKGProvider() override;

    void upsert_entity(const GraphEntity& entity) override;
    std::vector<GraphEntity> list_entities(const std::string& world_id) const override;
    void upsert_relation(const GraphRelation& relation) override;
    void delete_relation(const RelationKey& key) override;
    SubGraph query_subgraph(const std::string& world_id,
                             const std::vector<std::string>& entity_names,
                             const QueryFilters& filters) const override;
    NeighborGraph expand(const std::string& world_id,
                          const std::string& entity_name,
                          int radius,
                          const QueryFilters& filters) const override;
    PathResult find_paths(const std::string& world_id,
                           const std::string& source,
                           const std::string& target,
                           int max_depth,
                           const QueryFilters& filters) const override;
    void delete_world_graph(const std::string& world_id) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace merak::kg
