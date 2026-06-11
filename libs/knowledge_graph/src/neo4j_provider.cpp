#include <merak/kg/neo4j_provider.hpp>
#include <merak/kg/kg_models.hpp>

#include <neo4j-client.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace merak::kg {
namespace {

std::string now_iso_utc() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    struct tm buf;
    gmtime_r(&t, &buf);
    std::ostringstream oss;
    oss << std::put_time(&buf, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

struct SessionGuard {
    neo4j_session_t* session = nullptr;
    ~SessionGuard() { if (session) neo4j_close_session(session); }
    SessionGuard(const SessionGuard&) = delete;
    SessionGuard& operator=(const SessionGuard&) = delete;
    SessionGuard(SessionGuard&&) = delete;
    SessionGuard& operator=(SessionGuard&&) = delete;
};

const char* field_or_empty(const neo4j_result_t* record, int idx) {
    const char* val = neo4j_result_field(record, idx);
    return val ? val : "";
}

GraphRelation relation_from_neo4j_record(const neo4j_result_t* record) {
    GraphRelation r;
    r.source_id = field_or_empty(record, 0);
    r.target_id = field_or_empty(record, 1);
    r.source_name = field_or_empty(record, 2);
    r.target_name = field_or_empty(record, 3);
    r.kind_en = field_or_empty(record, 4);
    r.kind_cn = field_or_empty(record, 5);
    r.kind_custom = field_or_empty(record, 6);
    r.a_to_b_stance = stance_from_string(field_or_empty(record, 7));
    r.b_to_a_stance = stance_from_string(field_or_empty(record, 8));
    r.a_to_b_addressing = field_or_empty(record, 9);
    r.b_to_a_addressing = field_or_empty(record, 10);
    r.fact = field_or_empty(record, 11);
    r.description = field_or_empty(record, 12);
    r.world_id = field_or_empty(record, 13);
    r.created_at = field_or_empty(record, 14);
    r.updated_at = field_or_empty(record, 15);
    return r;
}

} // namespace

// ─── PIMPL implementation ───

struct Neo4jKGProvider::Impl {
    neo4j_driver_t* driver_ = nullptr;
    std::string uri_;
    std::string user_;
    std::string password_;
    std::string database_;

    Impl(std::string uri, std::string user, std::string password, std::string database)
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

    ~Impl() {
        if (driver_) neo4j_driver_destroy(driver_);
    }

    void ensure_constraints() {
        SessionGuard session{make_write_session()};
        neo4j_result_stream_t* results = neo4j_run(session.session,
            "CREATE CONSTRAINT entity_pk IF NOT EXISTS "
            "FOR (e:Entity) REQUIRE (e.source_id, e.world_id) IS UNIQUE",
            neo4j_null_params);
        neo4j_close_results(results);

        results = neo4j_run(session.session,
            "CREATE INDEX entity_name_lookup IF NOT EXISTS "
            "FOR (e:Entity) ON (e.world_id, e.name)",
            neo4j_null_params);
        neo4j_close_results(results);

        results = neo4j_run(session.session,
            "CREATE INDEX entity_world IF NOT EXISTS "
            "FOR (e:Entity) ON (e.world_id)",
            neo4j_null_params);
        neo4j_close_results(results);
    }

    neo4j_session_t* make_read_session() const {
        neo4j_session_config_t* config = neo4j_new_session_config();
        neo4j_session_config_set_default_access_mode(config, NEO4J_ACCESS_MODE_READ);
        neo4j_session_config_set_database(config, database_.c_str());
        auto session = neo4j_new_session(driver_, config);
        neo4j_free_session_config(config);
        if (!session) throw std::runtime_error("Neo4jKGProvider: failed to create read session");
        return session;
    }

    neo4j_session_t* make_write_session() const {
        neo4j_session_config_t* config = neo4j_new_session_config();
        neo4j_session_config_set_default_access_mode(config, NEO4J_ACCESS_MODE_WRITE);
        neo4j_session_config_set_database(config, database_.c_str());
        auto session = neo4j_new_session(driver_, config);
        neo4j_free_session_config(config);
        if (!session) throw std::runtime_error("Neo4jKGProvider: failed to create write session");
        return session;
    }

    void upsert_entity(const GraphEntity& entity) {
        SessionGuard session{make_write_session()};
        neo4j_result_stream_t* results = neo4j_run(session.session,
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
    }

    std::vector<GraphEntity> list_entities(const std::string& world_id) const {
        SessionGuard session{make_read_session()};
        neo4j_result_stream_t* results = neo4j_run(session.session,
            "MATCH (e:Entity {world_id: $world_id}) RETURN e.source_id, e.name, e.type, e.world_id, e.created_at",
            neo4j_map_of("world_id", world_id.c_str()));

        std::vector<GraphEntity> entities;
        neo4j_result_t* record;
        while ((record = neo4j_fetch_next(results)) != nullptr) {
            GraphEntity e;
            e.source_id = field_or_empty(record, 0);
            e.name = field_or_empty(record, 1);
            e.type = entity_type_from_string(field_or_empty(record, 2));
            e.world_id = field_or_empty(record, 3);
            e.created_at = field_or_empty(record, 4);
            entities.push_back(e);
        }
        neo4j_close_results(results);
        return entities;
    }

    void upsert_relation(const GraphRelation& relation) {
        SessionGuard session{make_write_session()};

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

        neo4j_result_stream_t* results = neo4j_run(session.session, cypher.c_str(),
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
    }

    void delete_relation(const RelationKey& key) {
        SessionGuard session{make_write_session()};
        neo4j_result_stream_t* results = neo4j_run(session.session,
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
    }

    SubGraph query_subgraph(const std::string& world_id,
                             const std::vector<std::string>& entity_names,
                             const QueryFilters& filters) const {
        SessionGuard session{make_read_session()};
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

        neo4j_result_stream_t* results = neo4j_run(session.session, cypher.c_str(),
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
        return sg;
    }

    NeighborGraph expand(const std::string& world_id,
                          const std::string& entity_name,
                          int radius,
                          const QueryFilters& filters) const {
        SessionGuard session{make_read_session()};
        std::string cypher = R"(
            MATCH (center:Entity {world_id: $world_id, name: $center_name})
            MATCH path = (center)-[rels:RELATES_TO*1..)" + std::to_string(radius) + R"(]-(neighbor:Entity {world_id: $world_id})
            WHERE all(rel IN rels WHERE rel.world_id = $world_id)
            WITH DISTINCT neighbor, center, rels
            UNWIND rels AS rel
            RETURN DISTINCT
                neighbor.name AS neighbor_name,
                startNode(rel).name AS src_name,
                endNode(rel).name AS tgt_name,
                rel.kind_en AS kind_en,
                rel.kind_cn AS kind_cn,
                rel.kind_custom AS kind_custom,
                rel.a_to_b_stance AS a_to_b_stance,
                rel.b_to_a_stance AS b_to_a_stance,
                rel.a_to_b_addressing AS a_to_b_addressing,
                rel.b_to_a_addressing AS b_to_a_addressing,
                rel.fact AS fact,
                rel.description AS description,
                rel.world_id AS world_id
            LIMIT $top_k
        )";

        neo4j_result_stream_t* results = neo4j_run(session.session, cypher.c_str(),
            neo4j_map_of("world_id", world_id.c_str(),
                         "center_name", entity_name.c_str(),
                         "top_k", std::to_string(filters.top_k).c_str()));

        NeighborGraph ng;
        ng.center_entity = entity_name;
        std::set<std::string> seen_neighbors;
        neo4j_result_t* record;
        while ((record = neo4j_fetch_next(results)) != nullptr) {
            std::string neighbor_name = neo4j_result_field(record, 0);
            if (seen_neighbors.insert(neighbor_name).second) {
                GraphEntity neighbor;
                neighbor.name = neighbor_name;
                ng.neighbor_entities.push_back(neighbor);
            }

            GraphRelation rel;
            rel.source_name = neo4j_result_field(record, 1);
            rel.target_name = neo4j_result_field(record, 2);
            rel.kind_en = neo4j_result_field(record, 3);
            rel.kind_cn = neo4j_result_field(record, 4);
            rel.kind_custom = neo4j_result_field(record, 5);
            rel.a_to_b_stance = stance_from_string(neo4j_result_field(record, 6));
            rel.b_to_a_stance = stance_from_string(neo4j_result_field(record, 7));
            rel.a_to_b_addressing = neo4j_result_field(record, 8);
            rel.b_to_a_addressing = neo4j_result_field(record, 9);
            rel.fact = neo4j_result_field(record, 10);
            rel.description = neo4j_result_field(record, 11);
            rel.world_id = neo4j_result_field(record, 12);
            ng.relations.push_back(rel);
        }
        neo4j_close_results(results);
        return ng;
    }

    PathResult find_paths(const std::string& world_id,
                           const std::string& source,
                           const std::string& target,
                           int max_depth,
                           const QueryFilters& filters) const {
        SessionGuard session{make_read_session()};
        std::string cypher = R"(
            MATCH path = (src:Entity {world_id: $world_id, name: $source})
                         -[rels:RELATES_TO*1..)" + std::to_string(max_depth) + R"(]->
                         (tgt:Entity {world_id: $world_id, name: $target})
            WHERE all(rel IN rels WHERE rel.world_id = $world_id)
            WITH path, rels, length(path) AS plen
            ORDER BY plen ASC
            LIMIT 10
            WITH collect({rels: rels, plen: plen}) AS paths
            UNWIND range(0, size(paths) - 1) AS path_idx
            WITH paths[path_idx] AS p, path_idx
            UNWIND range(0, p.plen - 1) AS hop
            RETURN path_idx,
                   startNode(p.rels[hop]).source_id AS src_id,
                   endNode(p.rels[hop]).source_id AS tgt_id,
                   startNode(p.rels[hop]).name AS src_name,
                   endNode(p.rels[hop]).name AS tgt_name,
                   p.rels[hop].kind_en AS kind_en,
                   p.rels[hop].kind_cn AS kind_cn,
                   p.rels[hop].kind_custom AS kind_custom,
                   p.rels[hop].a_to_b_stance AS a_to_b_stance,
                   p.rels[hop].b_to_a_stance AS b_to_a_stance,
                   p.rels[hop].a_to_b_addressing AS a_to_b_addressing,
                   p.rels[hop].b_to_a_addressing AS b_to_a_addressing,
                   p.rels[hop].fact AS fact,
                   p.rels[hop].description AS description,
                   p.rels[hop].world_id AS world_id,
                   p.rels[hop].created_at AS created_at,
                   p.rels[hop].updated_at AS updated_at
            ORDER BY path_idx, hop
        )";

        neo4j_result_stream_t* results = neo4j_run(session.session, cypher.c_str(),
            neo4j_map_of("world_id", world_id.c_str(),
                         "source", source.c_str(),
                         "target", target.c_str()));

        PathResult pr;
        pr.found = false;
        std::vector<GraphRelation> current_path;
        int current_path_idx = -1;

        neo4j_result_t* record;
        while ((record = neo4j_fetch_next(results)) != nullptr) {
            int path_idx = std::stoi(neo4j_result_field(record, 0));

            if (path_idx != current_path_idx) {
                if (!current_path.empty()) {
                    pr.paths.push_back(std::move(current_path));
                    current_path.clear();
                }
                current_path_idx = path_idx;
                pr.found = true;
            }

            GraphRelation rel;
            rel.source_id = neo4j_result_field(record, 1);
            rel.target_id = neo4j_result_field(record, 2);
            rel.source_name = neo4j_result_field(record, 3);
            rel.target_name = neo4j_result_field(record, 4);
            rel.kind_en = neo4j_result_field(record, 5);
            rel.kind_cn = neo4j_result_field(record, 6);
            rel.kind_custom = neo4j_result_field(record, 7);
            rel.a_to_b_stance = stance_from_string(neo4j_result_field(record, 8));
            rel.b_to_a_stance = stance_from_string(neo4j_result_field(record, 9));
            rel.a_to_b_addressing = neo4j_result_field(record, 10);
            rel.b_to_a_addressing = neo4j_result_field(record, 11);
            rel.fact = neo4j_result_field(record, 12);
            rel.description = neo4j_result_field(record, 13);
            rel.world_id = neo4j_result_field(record, 14);
            rel.created_at = neo4j_result_field(record, 15);
            rel.updated_at = neo4j_result_field(record, 16);
            current_path.push_back(rel);
        }
        if (!current_path.empty()) {
            pr.paths.push_back(std::move(current_path));
        }
        neo4j_close_results(results);
        return pr;
    }

    void delete_world_graph(const std::string& world_id) {
        SessionGuard session{make_write_session()};
        neo4j_result_stream_t* results = neo4j_run(session.session,
            "MATCH (e:Entity {world_id: $world_id}) "
            "DETACH DELETE e",
            neo4j_map_of("world_id", world_id.c_str()));
        neo4j_close_results(results);
    }
};

// ─── Outer class: delegate to impl_ ───

Neo4jKGProvider::Neo4jKGProvider(std::string uri, std::string user, std::string password,
                                 std::string database)
    : impl_(std::make_unique<Impl>(std::move(uri), std::move(user), std::move(password), std::move(database))) {}

Neo4jKGProvider::~Neo4jKGProvider() = default;

void Neo4jKGProvider::upsert_entity(const GraphEntity& entity) { impl_->upsert_entity(entity); }

std::vector<GraphEntity> Neo4jKGProvider::list_entities(const std::string& world_id) const {
    return impl_->list_entities(world_id);
}

void Neo4jKGProvider::upsert_relation(const GraphRelation& relation) { impl_->upsert_relation(relation); }

void Neo4jKGProvider::delete_relation(const RelationKey& key) { impl_->delete_relation(key); }

SubGraph Neo4jKGProvider::query_subgraph(const std::string& world_id,
                                          const std::vector<std::string>& entity_names,
                                          const QueryFilters& filters) const {
    return impl_->query_subgraph(world_id, entity_names, filters);
}

NeighborGraph Neo4jKGProvider::expand(const std::string& world_id,
                                       const std::string& entity_name,
                                       int radius,
                                       const QueryFilters& filters) const {
    return impl_->expand(world_id, entity_name, radius, filters);
}

PathResult Neo4jKGProvider::find_paths(const std::string& world_id,
                                        const std::string& source,
                                        const std::string& target,
                                        int max_depth,
                                        const QueryFilters& filters) const {
    return impl_->find_paths(world_id, source, target, max_depth, filters);
}

void Neo4jKGProvider::delete_world_graph(const std::string& world_id) { impl_->delete_world_graph(world_id); }

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
