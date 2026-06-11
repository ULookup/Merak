#include <merak/kg/kg_provider.hpp>
#include <merak/kg/kg_models.hpp>

#include <neo4j-client.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace merak::kg {
namespace {

std::string now_iso_utc() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

GraphRelation relation_from_neo4j_record(const neo4j_result_t* record) {
    GraphRelation r;
    r.source_id = neo4j_result_field(record, 0);
    r.target_id = neo4j_result_field(record, 1);
    r.source_name = neo4j_result_field(record, 2);
    r.target_name = neo4j_result_field(record, 3);
    r.kind_en = neo4j_result_field(record, 4);
    r.kind_cn = neo4j_result_field(record, 5);
    r.kind_custom = neo4j_result_field(record, 6);
    r.a_to_b_stance = stance_from_string(neo4j_result_field(record, 7));
    r.b_to_a_stance = stance_from_string(neo4j_result_field(record, 8));
    r.a_to_b_addressing = neo4j_result_field(record, 9);
    r.b_to_a_addressing = neo4j_result_field(record, 10);
    r.fact = neo4j_result_field(record, 11);
    r.description = neo4j_result_field(record, 12);
    r.world_id = neo4j_result_field(record, 13);
    r.created_at = neo4j_result_field(record, 14);
    r.updated_at = neo4j_result_field(record, 15);
    return r;
}

} // namespace

class Neo4jKGProvider : public KnowledgeGraphProvider {
public:
    Neo4jKGProvider(std::string uri, std::string user, std::string password,
                    std::string database = "merak")
        : uri_(std::move(uri)), user_(std::move(user)),
          password_(std::move(password)), database_(std::move(database))
    {
        neo4j_config_t* config = neo4j_new_config();
        neo4j_config_set_username(config, user_.c_str());
        neo4j_config_set_password(config, password_.c_str());
        driver_ = neo4j_new_driver(uri_.c_str(), config);
        if (!driver_) {
            throw std::runtime_error("Failed to create Neo4j driver for " + uri_);
        }
        ensure_constraints();
    }

    ~Neo4jKGProvider() override {
        if (driver_) neo4j_driver_destroy(driver_);
    }

    void upsert_entity(const GraphEntity& entity) override {
        auto session = make_write_session();
        neo4j_result_stream_t* results = neo4j_run(session,
            "MERGE (e:Entity {source_id: $source_id, world_id: $world_id}) "
            "SET e.name = $name, e.type = $type, e.created_at = $created_at",
            neo4j_map_of(
                "source_id", entity.source_id.c_str(),
                "world_id", entity.world_id.c_str(),
                "name", entity.name.c_str(),
                "type", to_string(entity.type).c_str(),
                "created_at", entity.created_at.empty() ? now_iso_utc().c_str() : entity.created_at.c_str()
            ));
        neo4j_close_results(results);
        neo4j_close_session(session);
    }

    std::vector<GraphEntity> list_entities(const std::string& world_id) const override {
        auto session = make_read_session();
        neo4j_result_stream_t* results = neo4j_run(session,
            "MATCH (e:Entity {world_id: $world_id}) RETURN e.source_id, e.name, e.type, e.world_id, e.created_at",
            neo4j_map_of("world_id", world_id.c_str()));

        std::vector<GraphEntity> entities;
        neo4j_result_t* record;
        while ((record = neo4j_fetch_next(results)) != nullptr) {
            GraphEntity e;
            e.source_id = neo4j_result_field(record, 0);
            e.name = neo4j_result_field(record, 1);
            e.type = EntityType::Agent;
            e.world_id = neo4j_result_field(record, 3);
            e.created_at = neo4j_result_field(record, 4);
            entities.push_back(e);
        }
        neo4j_close_results(results);
        neo4j_close_session(session);
        return entities;
    }

    void upsert_relation(const GraphRelation& relation) override {
        auto session = make_write_session();

        // Ensure both endpoint entities exist
        upsert_entity({relation.source_name, relation.source_type,
                       relation.source_id, relation.world_id, ""});
        upsert_entity({relation.target_name, relation.target_type,
                       relation.target_id, relation.world_id, ""});

        std::string cypher = R"(
            MATCH (a:Entity {source_id: $source_id, world_id: $world_id})
            MATCH (b:Entity {source_id: $target_id, world_id: $world_id})
            MERGE (a)-[r:RELATES_TO {kind_en: $kind_en}]->(b)
            SET r.source_id = $source_id,
                r.target_id = $target_id,
                r.source_name = $source_name,
                r.target_name = $target_name,
                r.kind_en = $kind_en,
                r.kind_cn = $kind_cn,
                r.kind_custom = $kind_custom,
                r.a_to_b_stance = $a_to_b_stance,
                r.b_to_a_stance = $b_to_a_stance,
                r.a_to_b_addressing = $a_to_b_addressing,
                r.b_to_a_addressing = $b_to_a_addressing,
                r.fact = $fact,
                r.description = $description,
                r.world_id = $world_id,
                r.created_at = $created_at,
                r.updated_at = $updated_at
        )";

        neo4j_result_stream_t* results = neo4j_run(session, cypher.c_str(),
            neo4j_map_of(
                "source_id", relation.source_id.c_str(),
                "target_id", relation.target_id.c_str(),
                "source_name", relation.source_name.c_str(),
                "target_name", relation.target_name.c_str(),
                "kind_en", relation.kind_en.c_str(),
                "kind_cn", relation.kind_cn.c_str(),
                "kind_custom", relation.kind_custom.c_str(),
                "a_to_b_stance", to_string(relation.a_to_b_stance).c_str(),
                "b_to_a_stance", to_string(relation.b_to_a_stance).c_str(),
                "a_to_b_addressing", relation.a_to_b_addressing.c_str(),
                "b_to_a_addressing", relation.b_to_a_addressing.c_str(),
                "fact", relation.fact.c_str(),
                "description", relation.description.c_str(),
                "world_id", relation.world_id.c_str(),
                "created_at", relation.created_at.empty() ? now_iso_utc().c_str() : relation.created_at.c_str(),
                "updated_at", now_iso_utc().c_str()
            ));
        neo4j_close_results(results);
        neo4j_close_session(session);
    }

    void delete_relation(const RelationKey& key) override {
        auto session = make_write_session();
        neo4j_result_stream_t* results = neo4j_run(session,
            "MATCH (a:Entity {source_id: $source_id, world_id: $world_id})"
            "-[r:RELATES_TO {kind_en: $kind_en}]->"
            "(b:Entity {source_id: $target_id, world_id: $world_id}) "
            "DELETE r",
            neo4j_map_of(
                "source_id", key.source_id.c_str(),
                "target_id", key.target_id.c_str(),
                "kind_en", key.kind_en.c_str(),
                "world_id", key.world_id.c_str()
            ));
        neo4j_close_results(results);
        neo4j_close_session(session);
    }

    SubGraph query_subgraph(const std::string& world_id,
                             const std::vector<std::string>& entity_names,
                             const QueryFilters& filters) const override {
        auto session = make_read_session();
        nlohmann::json names_json = entity_names;
        std::string names_str = names_json.dump();

        std::string cypher = R"(
            MATCH (a:Entity {world_id: $world_id})-[r:RELATES_TO]->(b:Entity {world_id: $world_id})
            WHERE a.name IN )" + names_str + R"( AND b.name IN )" + names_str + R"(
            RETURN a.source_id, b.source_id, a.name, b.name,
                   r.kind_en, r.kind_cn, r.kind_custom,
                   r.a_to_b_stance, r.b_to_a_stance,
                   r.a_to_b_addressing, r.b_to_a_addressing,
                   r.fact, r.description, r.world_id,
                   r.created_at, r.updated_at
            LIMIT $top_k
        )";

        neo4j_result_stream_t* results = neo4j_run(session, cypher.c_str(),
            neo4j_map_of("world_id", world_id.c_str(),
                         "top_k", std::to_string(filters.top_k).c_str()));

        SubGraph sg;
        neo4j_result_t* record;
        while ((record = neo4j_fetch_next(results)) != nullptr) {
            auto rel = relation_from_neo4j_record(record);
            sg.relations.push_back(rel);
            sg.fact_summaries.push_back(
                rel.source_name + " \xE2\x86\x92 " + rel.target_name + ": " + rel.kind_cn +
                " (" + to_string(rel.a_to_b_stance) + "/" + to_string(rel.b_to_a_stance) + ")");
        }
        neo4j_close_results(results);
        neo4j_close_session(session);
        return sg;
    }

    NeighborGraph expand(const std::string& world_id,
                          const std::string& entity_name,
                          int radius,
                          const QueryFilters& filters) const override {
        auto session = make_read_session();
        std::string cypher = R"(
            MATCH (center:Entity {world_id: $world_id, name: $center_name})
            MATCH (center)-[r:RELATES_TO*1..)" + std::to_string(radius) + R"(]-(neighbor:Entity {world_id: $world_id})
            WHERE all(rel IN r WHERE rel.world_id = $world_id)
            RETURN DISTINCT neighbor, r
            LIMIT $top_k
        )";

        neo4j_result_stream_t* results = neo4j_run(session, cypher.c_str(),
            neo4j_map_of("world_id", world_id.c_str(),
                         "center_name", entity_name.c_str(),
                         "top_k", std::to_string(filters.top_k).c_str()));

        NeighborGraph ng;
        ng.center_entity = entity_name;
        neo4j_result_t* record;
        while ((record = neo4j_fetch_next(results)) != nullptr) {
            GraphEntity neighbor;
            neighbor.name = neo4j_result_field(record, 0);
            ng.neighbor_entities.push_back(neighbor);
        }
        neo4j_close_results(results);
        neo4j_close_session(session);
        return ng;
    }

    PathResult find_paths(const std::string& world_id,
                           const std::string& source,
                           const std::string& target,
                           int max_depth,
                           const QueryFilters& filters) const override {
        auto session = make_read_session();
        std::string cypher = R"(
            MATCH path = (src:Entity {world_id: $world_id, name: $source})
                         -[r:RELATES_TO*1..)" + std::to_string(max_depth) + R"(]->
                         (tgt:Entity {world_id: $world_id, name: $target})
            WHERE all(rel IN relationships(path) WHERE rel.world_id = $world_id)
            RETURN path
            ORDER BY length(path) ASC
            LIMIT 10
        )";

        neo4j_result_stream_t* results = neo4j_run(session, cypher.c_str(),
            neo4j_map_of("world_id", world_id.c_str(),
                         "source", source.c_str(),
                         "target", target.c_str()));

        PathResult pr;
        pr.found = false;
        neo4j_result_t* record;
        while ((record = neo4j_fetch_next(results)) != nullptr) {
            pr.found = true;
            std::vector<GraphRelation> path;
            pr.paths.push_back(path);
        }
        neo4j_close_results(results);
        neo4j_close_session(session);
        return pr;
    }

    void delete_world_graph(const std::string& world_id) override {
        auto session = make_write_session();
        neo4j_result_stream_t* results = neo4j_run(session,
            "MATCH (e:Entity {world_id: $world_id}) "
            "DETACH DELETE e",
            neo4j_map_of("world_id", world_id.c_str()));
        neo4j_close_results(results);
        neo4j_close_session(session);
    }

private:
    void ensure_constraints() {
        auto session = make_write_session();
        neo4j_result_stream_t* results = neo4j_run(session,
            "CREATE CONSTRAINT entity_pk IF NOT EXISTS "
            "FOR (e:Entity) REQUIRE (e.source_id, e.world_id) IS UNIQUE",
            neo4j_null_params);
        neo4j_close_results(results);

        results = neo4j_run(session,
            "CREATE INDEX entity_name_lookup IF NOT EXISTS "
            "FOR (e:Entity) ON (e.world_id, e.name)",
            neo4j_null_params);
        neo4j_close_results(results);

        results = neo4j_run(session,
            "CREATE INDEX entity_world IF NOT EXISTS "
            "FOR (e:Entity) ON (e.world_id)",
            neo4j_null_params);
        neo4j_close_results(results);
        neo4j_close_session(session);
    }

    neo4j_session_t* make_read_session() const {
        neo4j_session_config_t* config = neo4j_new_session_config();
        neo4j_session_config_set_default_access_mode(config, NEO4J_ACCESS_MODE_READ);
        neo4j_session_config_set_database(config, database_.c_str());
        auto session = neo4j_new_session(driver_, config);
        neo4j_free_session_config(config);
        return session;
    }

    neo4j_session_t* make_write_session() const {
        neo4j_session_config_t* config = neo4j_new_session_config();
        neo4j_session_config_set_default_access_mode(config, NEO4J_ACCESS_MODE_WRITE);
        neo4j_session_config_set_database(config, database_.c_str());
        auto session = neo4j_new_session(driver_, config);
        neo4j_free_session_config(config);
        return session;
    }

    neo4j_driver_t* driver_ = nullptr;
    std::string uri_;
    std::string user_;
    std::string password_;
    std::string database_;
};

// ─── Formatting helpers ───

std::string KnowledgeGraphProvider::subgraph_to_markdown(const SubGraph& sg) {
    if (sg.relations.empty()) return "";
    std::ostringstream os;
    os << "## 角色关系\n\n";
    for (auto& r : sg.relations) {
        os << "- **" << r.source_name << "** → **" << r.target_name
           << "**: " << r.kind_cn
           << " (" << to_string(r.a_to_b_stance) << "/" << to_string(r.b_to_a_stance) << ")";
        if (!r.fact.empty()) os << " — " << r.fact;
        os << "\n";
    }
    return os.str();
}

std::string KnowledgeGraphProvider::neighbor_graph_to_markdown(const NeighborGraph& ng) {
    std::ostringstream os;
    os << "## " << ng.center_entity << " 的关系网络\n\n";
    for (auto& r : ng.relations) {
        os << "- " << r.source_name << " → " << r.target_name
           << ": " << r.kind_cn << "\n";
    }
    return os.str();
}

std::string KnowledgeGraphProvider::path_result_to_markdown(const PathResult& pr) {
    if (!pr.found) return "*未找到路径*\n";
    std::ostringstream os;
    for (size_t i = 0; i < pr.paths.size(); ++i) {
        os << "**路径 " << (i + 1) << "** ("
           << pr.paths[i].size() << " 跳): ";
        for (size_t j = 0; j < pr.paths[i].size(); ++j) {
            if (j > 0) os << " → ";
            os << pr.paths[i][j].source_name;
        }
        os << " → " << pr.paths[i].back().target_name << "\n";
    }
    return os.str();
}

} // namespace merak::kg
