#include <merak/worldbuilding/world_store.hpp>

#include <merak/worldbuilding/ids.hpp>
#include <merak/worldbuilding/pg_helpers.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace merak::worldbuilding {
namespace {

constexpr std::array<std::string_view, 14> kWorldDirectories = {
    "world_knowledge", "god",          "managers/map",    "managers/history",
    "managers/magic",  "managers/faction", "managers/relation", "agents",
    "scenes",          "chapters",     "arcs",            "secrets",
    "foreshadows",     "sessions"};

AgentKind agent_kind_from_string(const std::string& value) {
    if (value == "god") return AgentKind::God;
    if (value == "map_manager") return AgentKind::MapManager;
    if (value == "history_manager") return AgentKind::HistoryManager;
    if (value == "magic_system_manager") return AgentKind::MagicSystemManager;
    if (value == "faction_manager") return AgentKind::FactionManager;
    if (value == "relation_manager") return AgentKind::RelationManager;
    if (value == "group") return AgentKind::Group;
    if (value == "individual") return AgentKind::Individual;
    if (value == "writer") return AgentKind::Writer;
    throw std::runtime_error("unknown agent kind: " + value);
}

WorldMeta world_meta_from_row(const PgResult& res, int row) {
    WorldMeta meta{
        .id = res.get(row, 0),
        .name = res.get(row, 1),
        .description = res.get(row, 2),
        .created_at = res.get(row, 3),
        .updated_at = res.get(row, 4),
    };
    if (res.nfields() > 5) {
        std::string config_str = res.get(row, 5);
        if (!config_str.empty()) {
            try { meta.config = nlohmann::json::parse(config_str); } catch (...) {}
        }
    }
    return meta;
}

AgentRecord agent_record_from_row(const PgResult& res, int row) {
    return AgentRecord{
        .id = res.get(row, 0),
        .world_id = res.get(row, 1),
        .name = res.get(row, 2),
        .display_name = res.get(row, 3),
        .created_at = res.get(row, 5),
        .updated_at = res.get(row, 6),
        .kind = agent_kind_from_string(res.get(row, 4)),
    };
}

WorldKnowledge world_knowledge_from_row(const PgResult& res, int row) {
    WorldKnowledge wk;
    wk.id = res.get(row, 0);
    wk.category = res.get(row, 1);
    wk.content = res.get(row, 2);
    wk.created_at = res.get(row, 3);
    try { wk.tags = nlohmann::json::parse(res.get(row, 4)).get<std::vector<std::string>>(); } catch (...) {}
    try { wk.aliases = nlohmann::json::parse(res.get(row, 5)).get<std::vector<std::string>>(); } catch (...) {}
    try { wk.related_ids = nlohmann::json::parse(res.get(row, 6)).get<std::vector<std::string>>(); } catch (...) {}
    return wk;
}

Location location_from_row(const PgResult& res, int row) {
    Location loc;
    loc.id = res.get(row, 0);
    loc.name = res.get(row, 1);
    loc.description = res.get(row, 2);
    loc.region = res.get(row, 3);
    if (!res.is_null(row, 4)) {
        loc.parent_location_id = res.get(row, 4);
    }
    loc.created_at = res.get(row, 5);
    return loc;
}

} // namespace

WorldStore::WorldStore(std::string_view pg_conninfo, std::filesystem::path data_root)
    : data_root_(std::move(data_root)),
      pool_(std::make_unique<PgPool>(pg_conninfo)) {}

WorldStore::~WorldStore() = default;

void WorldStore::initialize() {
    std::filesystem::create_directories(data_root_);
    std::filesystem::create_directories(data_root_ / "worlds");
}

WorldMeta WorldStore::create_world(const std::string& name,
                                   const std::string& description) {
    initialize();

    const auto timestamp = now_iso_utc();
    WorldMeta world{
        .id = make_id("world"),
        .name = name,
        .description = description,
        .created_at = timestamp,
        .updated_at = timestamp,
    };

    const auto root = world_path(world.id);
    PgConn conn(*pool_);

    try {
        std::filesystem::create_directories(root);
        for (const auto directory : kWorldDirectories) {
            std::filesystem::create_directories(root / directory);
        }

        {
            std::ofstream timeline(root / "timeline.json");
            if (!timeline) {
                throw std::runtime_error("create world timeline failed");
            }
            timeline << R"({"events":[]})";
        }

        conn.exec("BEGIN");
        conn.execute(
            "INSERT INTO worlds(id, name, description, created_at, updated_at) "
            "VALUES($1, $2, $3, $4, $5)",
            {world.id, world.name, world.description, world.created_at, world.updated_at});

        const auto agent_id = make_id("agent");
        conn.execute(
            "INSERT INTO agents(id, world_id, name, display_name, kind, created_at, updated_at) "
            "VALUES($1, $2, $3, $4, $5, $6, $7)",
            {agent_id, world.id, "god", "god", to_string(AgentKind::God), timestamp, timestamp});

        const auto rm_id = make_id("agent");
        conn.execute(
            "INSERT INTO agents(id, world_id, name, display_name, kind, created_at, updated_at) "
            "VALUES($1, $2, $3, $4, $5, $6, $7)",
            {rm_id, world.id, "relation_manager", "relation_manager",
             to_string(AgentKind::RelationManager), timestamp, timestamp});

        conn.exec("COMMIT");
    } catch (...) {
        try { conn.exec("ROLLBACK"); } catch (...) {}
        remove_all_no_throw(root);
        throw;
    }

    return world;
}

WorldMeta WorldStore::update_world(const std::string& world_id,
                                    const std::optional<std::string>& name,
                                    const std::optional<std::string>& description) {
    initialize();
    auto existing = get_world(world_id);
    if (!existing) throw std::runtime_error("world not found: " + world_id);

    std::string new_name = name.value_or(existing->name);
    std::string new_desc = description.value_or(existing->description);
    std::string timestamp = now_iso_utc();

    PgConn conn(*pool_);
    conn.execute(
        "UPDATE worlds SET name = $1, description = $2, updated_at = $3 WHERE id = $4",
        {new_name, new_desc, timestamp, world_id});

    return WorldMeta{
        .id = world_id,
        .name = new_name,
        .description = new_desc,
        .created_at = existing->created_at,
        .updated_at = timestamp,
    };
}

std::optional<WorldMeta>
WorldStore::get_world(const std::string& world_id) const {
    PgConn conn(*pool_);
    auto res = conn.query(
        "SELECT id, name, description, created_at, updated_at "
        "FROM worlds WHERE id = $1",
        {world_id});
    if (res.ntuples() == 0) return std::nullopt;
    return world_meta_from_row(res, 0);
}

std::vector<WorldMeta> WorldStore::list_worlds() const {
    PgConn conn(*pool_);
    auto res = conn.query(
        "SELECT id, name, description, created_at, updated_at "
        "FROM worlds ORDER BY created_at ASC, id ASC");
    std::vector<WorldMeta> worlds;
    for (int i = 0; i < res.ntuples(); i++) {
        worlds.push_back(world_meta_from_row(res, i));
    }
    return worlds;
}

bool WorldStore::delete_world(const std::string& world_id) {
    PgConn conn(*pool_);

    auto check = conn.query("SELECT 1 FROM worlds WHERE id = $1", {world_id});
    if (check.ntuples() == 0) return false;

    const auto root = world_path(world_id);

    conn.exec("BEGIN");
    try {
        conn.execute("DELETE FROM world_knowledge WHERE world_id = $1", {world_id});
        conn.execute("DELETE FROM locations WHERE world_id = $1", {world_id});
        conn.execute("DELETE FROM agents WHERE world_id = $1", {world_id});
        int affected = conn.execute("DELETE FROM worlds WHERE id = $1", {world_id});
        conn.exec("COMMIT");
        if (std::filesystem::exists(root)) {
            std::filesystem::remove_all(root);
        }
        return affected > 0;
    } catch (...) {
        try { conn.exec("ROLLBACK"); } catch (...) {}
        throw;
    }
}

void WorldStore::add_world_knowledge(const std::string& world_id,
                                     WorldKnowledge item) {
    if (item.id.empty()) item.id = make_id("knowledge");
    if (item.created_at.empty()) item.created_at = now_iso_utc();

    PgConn conn(*pool_);
    conn.execute(
        "INSERT INTO world_knowledge(id, world_id, category, content, created_at, "
        "tags, aliases, related_ids) "
        "VALUES($1, $2, $3, $4, $5, $6, $7, $8)",
        {item.id, world_id, item.category, item.content, item.created_at,
         nlohmann::json(item.tags).dump(),
         nlohmann::json(item.aliases).dump(),
         nlohmann::json(item.related_ids).dump()});
}

std::vector<WorldKnowledge>
WorldStore::get_world_knowledge(const std::string& world_id,
                                const std::string& category) const {
    PgConn conn(*pool_);
    PgResult res;
    if (!category.empty()) {
        res = conn.query(
            "SELECT id, category, content, created_at, tags, aliases, related_ids "
            "FROM world_knowledge "
            "WHERE world_id = $1 AND category = $2 "
            "ORDER BY created_at ASC, id ASC",
            {world_id, category});
    } else {
        res = conn.query(
            "SELECT id, category, content, created_at, tags, aliases, related_ids "
            "FROM world_knowledge "
            "WHERE world_id = $1 "
            "ORDER BY created_at ASC, id ASC",
            {world_id});
    }

    std::vector<WorldKnowledge> items;
    for (int i = 0; i < res.ntuples(); i++) {
        items.push_back(world_knowledge_from_row(res, i));
    }
    return items;
}

std::vector<WorldKnowledge>
WorldStore::search_world_knowledge(const std::string& world_id,
                                    const std::string& query,
                                    const std::string& category,
                                    int max_results) const {
    PgConn conn(*pool_);
    std::vector<WorldKnowledge> items;

    // Try hybrid search (FTS + vector weighting via stored function)
    try {
        int limit = std::clamp(max_results, 0, 100);
        auto res = conn.query(
            "SELECT id, category, content, created_at, tags, aliases, related_ids "
            "FROM hybrid_search_knowledge($1, $2, $3, $4)",
            {world_id, query, category, std::to_string(limit)});

        for (int i = 0; i < res.ntuples(); i++) {
            items.push_back(world_knowledge_from_row(res, i));
        }
    } catch (...) {}

    if (!items.empty()) return items;

    // Fallback: LIKE-based search
    PgResult fallback;
    int limit = std::clamp(max_results, 0, 100);
    if (!category.empty()) {
        fallback = conn.query(
            "SELECT id, category, content, created_at, tags, aliases, related_ids "
            "FROM world_knowledge "
            "WHERE world_id = $1 AND category = $2 AND content LIKE $3 "
            "ORDER BY created_at DESC, id DESC LIMIT $4",
            {world_id, category, "%" + query + "%", std::to_string(limit)});
    } else {
        fallback = conn.query(
            "SELECT id, category, content, created_at, tags, aliases, related_ids "
            "FROM world_knowledge "
            "WHERE world_id = $1 AND content LIKE $2 "
            "ORDER BY created_at DESC, id DESC LIMIT $3",
            {world_id, "%" + query + "%", std::to_string(limit)});
    }

    for (int i = 0; i < fallback.ntuples(); i++) {
        items.push_back(world_knowledge_from_row(fallback, i));
    }
    return items;
}

Location WorldStore::add_location(const std::string& world_id, Location location) {
    if (location.id.empty()) location.id = make_id("loc");
    if (location.created_at.empty()) location.created_at = now_iso_utc();

    PgConn conn(*pool_);
    conn.execute(
        "INSERT INTO locations(id, world_id, name, description, region, "
        "parent_location_id, created_at) "
        "VALUES($1, $2, $3, $4, $5, NULLIF($6, ''), $7)",
        {location.id, world_id, location.name, location.description,
         location.region,
         location.parent_location_id.has_value()
             ? location.parent_location_id.value()
             : "",
         location.created_at});
    return location;
}

std::optional<Location>
WorldStore::get_location(const std::string& world_id,
                         const std::string& location_id) const {
    PgConn conn(*pool_);
    auto res = conn.query(
        "SELECT id, name, description, region, parent_location_id, created_at "
        "FROM locations WHERE world_id = $1 AND id = $2",
        {world_id, location_id});
    if (res.ntuples() == 0) return std::nullopt;
    return location_from_row(res, 0);
}

std::vector<Location>
WorldStore::list_locations(const std::string& world_id) const {
    PgConn conn(*pool_);
    auto res = conn.query(
        "SELECT id, name, description, region, parent_location_id, created_at "
        "FROM locations WHERE world_id = $1 "
        "ORDER BY created_at ASC, id ASC",
        {world_id});

    std::vector<Location> locations;
    for (int i = 0; i < res.ntuples(); i++) {
        locations.push_back(location_from_row(res, i));
    }
    return locations;
}

std::vector<AgentRecord>
WorldStore::list_agents(const std::string& world_id) const {
    PgConn conn(*pool_);
    auto res = conn.query(
        "SELECT id, world_id, name, display_name, kind, created_at, updated_at "
        "FROM agents WHERE world_id = $1 "
        "ORDER BY created_at ASC, id ASC",
        {world_id});

    std::vector<AgentRecord> agents;
    for (int i = 0; i < res.ntuples(); i++) {
        agents.push_back(agent_record_from_row(res, i));
    }
    return agents;
}

std::filesystem::path
WorldStore::world_path(const std::string& world_id) const {
    return data_root_ / "worlds" / world_id;
}

} // namespace merak::worldbuilding
