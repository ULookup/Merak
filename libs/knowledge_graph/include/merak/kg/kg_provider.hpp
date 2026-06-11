#pragma once

#include <merak/kg/kg_models.hpp>

#include <memory>
#include <string>
#include <vector>

namespace merak::kg {

class KnowledgeGraphProvider {
public:
    virtual ~KnowledgeGraphProvider() = default;

    // ─── Entity ───
    virtual void upsert_entity(const GraphEntity& entity) = 0;
    virtual std::vector<GraphEntity> list_entities(const std::string& world_id) const = 0;

    // ─── Relation ───
    virtual void upsert_relation(const GraphRelation& relation) = 0;
    virtual void delete_relation(const RelationKey& key) = 0;

    // ─── Query ───
    virtual SubGraph query_subgraph(
        const std::string& world_id,
        const std::vector<std::string>& entity_names,
        const QueryFilters& filters
    ) const = 0;

    virtual NeighborGraph expand(
        const std::string& world_id,
        const std::string& entity_name,
        int radius,
        const QueryFilters& filters
    ) const = 0;

    virtual PathResult find_paths(
        const std::string& world_id,
        const std::string& source,
        const std::string& target,
        int max_depth,
        const QueryFilters& filters
    ) const = 0;

    // ─── Maintenance ───
    virtual void delete_world_graph(const std::string& world_id) = 0;

    // ─── Formatting helpers (shared across Provider impls) ───
    static std::string subgraph_to_markdown(const SubGraph& sg);
    static std::string neighbor_graph_to_markdown(const NeighborGraph& ng);
    static std::string path_result_to_markdown(const PathResult& pr);
};

} // namespace merak::kg
