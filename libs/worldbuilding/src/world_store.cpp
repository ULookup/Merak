#include <merak/worldbuilding/world_store.hpp>

#include <merak/worldbuilding/ids.hpp>
#include <merak/worldbuilding/sqlite_helpers.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace merak::worldbuilding {
namespace {

constexpr std::array<std::string_view, 13> kWorldDirectories = {
    "world_knowledge", "god",         "managers/map",   "managers/history",
    "managers/magic",  "managers/faction", "agents",    "scenes",
    "chapters",        "arcs",        "secrets",        "foreshadows",
    "sessions"};

AgentKind agent_kind_from_string(const std::string& value) {
    if (value == "god") {
        return AgentKind::God;
    }
    if (value == "map_manager") {
        return AgentKind::MapManager;
    }
    if (value == "history_manager") {
        return AgentKind::HistoryManager;
    }
    if (value == "magic_system_manager") {
        return AgentKind::MagicSystemManager;
    }
    if (value == "faction_manager") {
        return AgentKind::FactionManager;
    }
    if (value == "group") {
        return AgentKind::Group;
    }
    if (value == "individual") {
        return AgentKind::Individual;
    }
    throw std::runtime_error("unknown agent kind: " + value);
}

WorldMeta read_world_meta(Statement& statement) {
    return WorldMeta{
        .id = column_text(statement, 0),
        .name = column_text(statement, 1),
        .description = column_text(statement, 2),
        .created_at = column_text(statement, 3),
        .updated_at = column_text(statement, 4),
    };
}

AgentRecord read_agent_record(Statement& statement) {
    return AgentRecord{
        .id = column_text(statement, 0),
        .world_id = column_text(statement, 1),
        .name = column_text(statement, 2),
        .display_name = column_text(statement, 3),
        .created_at = column_text(statement, 5),
        .updated_at = column_text(statement, 6),
        .kind = agent_kind_from_string(column_text(statement, 4)),
    };
}

void execute_bound(Statement& statement) {
    if (statement.step()) {
        throw std::runtime_error("unexpected sqlite row");
    }
}

void rollback_no_throw(SqliteDb& db) noexcept {
    try {
        db.exec("ROLLBACK");
    } catch (...) {
    }
}

void remove_all_no_throw(const std::filesystem::path& path) noexcept {
    try {
        std::filesystem::remove_all(path);
    } catch (...) {
    }
}

} // namespace

WorldStore::WorldStore(std::filesystem::path data_root)
    : data_root_(std::move(data_root)) {}

void WorldStore::initialize() {
    std::filesystem::create_directories(data_root_);
    std::filesystem::create_directories(data_root_ / "worlds");

    SqliteDb db(database_path().string());
    db.exec("PRAGMA foreign_keys = ON");
    db.exec(R"sql(
        CREATE TABLE IF NOT EXISTS worlds(
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            description TEXT NOT NULL,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
    )sql");
    db.exec(R"sql(
        CREATE TABLE IF NOT EXISTS world_knowledge(
            id TEXT PRIMARY KEY,
            world_id TEXT NOT NULL,
            category TEXT NOT NULL,
            content TEXT NOT NULL,
            created_at TEXT NOT NULL,
            FOREIGN KEY(world_id) REFERENCES worlds(id) ON DELETE CASCADE
        )
    )sql");
    db.exec(R"sql(
        CREATE TABLE IF NOT EXISTS agents(
            id TEXT PRIMARY KEY,
            world_id TEXT NOT NULL,
            name TEXT NOT NULL,
            display_name TEXT NOT NULL,
            kind TEXT NOT NULL,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            FOREIGN KEY(world_id) REFERENCES worlds(id) ON DELETE CASCADE
        )
    )sql");
    db.exec(R"sql(
        CREATE UNIQUE INDEX IF NOT EXISTS one_god_agent_per_world
        ON agents(world_id, kind)
        WHERE kind = 'god'
    )sql");
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
    SqliteDb db(database_path().string());
    db.exec("PRAGMA foreign_keys = ON");

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

        db.exec("BEGIN");
        Statement insert_world(db, R"sql(
            INSERT INTO worlds(id, name, description, created_at, updated_at)
            VALUES(?1, ?2, ?3, ?4, ?5)
        )sql");
        bind_text(insert_world, 1, world.id);
        bind_text(insert_world, 2, world.name);
        bind_text(insert_world, 3, world.description);
        bind_text(insert_world, 4, world.created_at);
        bind_text(insert_world, 5, world.updated_at);
        execute_bound(insert_world);

        Statement insert_agent(db, R"sql(
            INSERT INTO agents(id, world_id, name, display_name, kind,
                               created_at, updated_at)
            VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)
        )sql");
        const auto agent_id = make_id("agent");
        bind_text(insert_agent, 1, agent_id);
        bind_text(insert_agent, 2, world.id);
        bind_text(insert_agent, 3, "god");
        bind_text(insert_agent, 4, "god");
        bind_text(insert_agent, 5, to_string(AgentKind::God));
        bind_text(insert_agent, 6, timestamp);
        bind_text(insert_agent, 7, timestamp);
        execute_bound(insert_agent);

        db.exec("COMMIT");
    } catch (...) {
        rollback_no_throw(db);
        remove_all_no_throw(root);
        throw;
    }

    return world;
}

std::optional<WorldMeta>
WorldStore::get_world(const std::string& world_id) const {
    SqliteDb db(database_path().string());
    Statement query(db, R"sql(
        SELECT id, name, description, created_at, updated_at
        FROM worlds
        WHERE id = ?1
    )sql");
    bind_text(query, 1, world_id);

    if (!query.step()) {
        return std::nullopt;
    }
    return read_world_meta(query);
}

std::vector<WorldMeta> WorldStore::list_worlds() const {
    SqliteDb db(database_path().string());
    Statement query(db, R"sql(
        SELECT id, name, description, created_at, updated_at
        FROM worlds
        ORDER BY created_at ASC, id ASC
    )sql");

    std::vector<WorldMeta> worlds;
    while (query.step()) {
        worlds.push_back(read_world_meta(query));
    }
    return worlds;
}

bool WorldStore::delete_world(const std::string& world_id) {
    SqliteDb db(database_path().string());
    db.exec("PRAGMA foreign_keys = ON");

    {
        Statement exists(db, "SELECT 1 FROM worlds WHERE id = ?1");
        bind_text(exists, 1, world_id);
        if (!exists.step()) {
            return false;
        }
    }

    const auto root = world_path(world_id);
    if (std::filesystem::exists(root)) {
        std::filesystem::remove_all(root);
    }

    db.exec("BEGIN");
    try {
        Statement delete_knowledge(
            db, "DELETE FROM world_knowledge WHERE world_id = ?1");
        bind_text(delete_knowledge, 1, world_id);
        execute_bound(delete_knowledge);

        Statement delete_agents(db, "DELETE FROM agents WHERE world_id = ?1");
        bind_text(delete_agents, 1, world_id);
        execute_bound(delete_agents);

        Statement delete_world(db, "DELETE FROM worlds WHERE id = ?1");
        bind_text(delete_world, 1, world_id);
        execute_bound(delete_world);

        const bool removed = sqlite3_changes(db.get()) > 0;
        db.exec("COMMIT");
        return removed;
    } catch (...) {
        rollback_no_throw(db);
        throw;
    }
}

void WorldStore::add_world_knowledge(const std::string& world_id,
                                     WorldKnowledge item) {
    SqliteDb db(database_path().string());
    db.exec("PRAGMA foreign_keys = ON");

    if (item.id.empty()) {
        item.id = make_id("knowledge");
    }
    if (item.created_at.empty()) {
        item.created_at = now_iso_utc();
    }

    Statement insert(db, R"sql(
        INSERT INTO world_knowledge(id, world_id, category, content, created_at)
        VALUES(?1, ?2, ?3, ?4, ?5)
    )sql");
    bind_text(insert, 1, item.id);
    bind_text(insert, 2, world_id);
    bind_text(insert, 3, item.category);
    bind_text(insert, 4, item.content);
    bind_text(insert, 5, item.created_at);
    execute_bound(insert);
}

std::vector<WorldKnowledge>
WorldStore::get_world_knowledge(const std::string& world_id,
                                const std::string& category) const {
    SqliteDb db(database_path().string());
    const bool filter_by_category = !category.empty();
    Statement query(db, filter_by_category ?
                            R"sql(
                                SELECT id, category, content, created_at
                                FROM world_knowledge
                                WHERE world_id = ?1 AND category = ?2
                                ORDER BY created_at ASC, id ASC
                            )sql" :
                            R"sql(
                                SELECT id, category, content, created_at
                                FROM world_knowledge
                                WHERE world_id = ?1
                                ORDER BY created_at ASC, id ASC
                            )sql");
    bind_text(query, 1, world_id);
    if (filter_by_category) {
        bind_text(query, 2, category);
    }

    std::vector<WorldKnowledge> items;
    while (query.step()) {
        items.push_back(WorldKnowledge{
            .id = column_text(query, 0),
            .category = column_text(query, 1),
            .content = column_text(query, 2),
            .created_at = column_text(query, 3),
        });
    }
    return items;
}

std::vector<AgentRecord>
WorldStore::list_agents(const std::string& world_id) const {
    SqliteDb db(database_path().string());
    Statement query(db, R"sql(
        SELECT id, world_id, name, display_name, kind, created_at, updated_at
        FROM agents
        WHERE world_id = ?1
        ORDER BY created_at ASC, id ASC
    )sql");
    bind_text(query, 1, world_id);

    std::vector<AgentRecord> agents;
    while (query.step()) {
        agents.push_back(read_agent_record(query));
    }
    return agents;
}

std::filesystem::path
WorldStore::world_path(const std::string& world_id) const {
    return data_root_ / "worlds" / world_id;
}

std::filesystem::path WorldStore::database_path() const {
    return data_root_ / "worlds.sqlite3";
}

} // namespace merak::worldbuilding
