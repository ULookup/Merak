#include <gtest/gtest.h>
#include <merak/worldbuilding/ids.hpp>
#include <merak/worldbuilding/sqlite_helpers.hpp>
#include <merak/worldbuilding/world_store.hpp>
#include <merak/worldbuilding/world_models.hpp>

#include "test_helpers.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

using namespace merak::worldbuilding;
using namespace merak::worldbuilding::test;

namespace {

std::filesystem::path temp_dir() {
    auto path =
        std::filesystem::temp_directory_path() / make_id("world_store_test");
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

} // namespace

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

TEST(WorldbuildingModels, PublicEnumStringConversionsAreStable) {
    EXPECT_EQ(to_string(ForeshadowHintLevel::Subtle), "subtle");
    EXPECT_EQ(to_string(ForeshadowHintLevel::Visible), "visible");
    EXPECT_EQ(to_string(ForeshadowHintLevel::Obvious), "obvious");
    EXPECT_EQ(to_string(ForeshadowCreatedBy::Author), "author");
    EXPECT_EQ(to_string(ForeshadowCreatedBy::GodAgentDetected),
              "god_agent_detected");
    EXPECT_EQ(to_string(NarrativeTemplate::ThreeAct), "three_act");
    EXPECT_EQ(to_string(NarrativeTemplate::FourAct), "four_act");
    EXPECT_EQ(to_string(NarrativeTemplate::HerosJourney), "heros_journey");
    EXPECT_EQ(to_string(NarrativeTemplate::Freeform), "freeform");
    EXPECT_EQ(to_string(KnowledgeState::Public), "public");
    EXPECT_EQ(to_string(KnowledgeState::Secret), "secret");
    EXPECT_EQ(to_string(KnowledgeState::Unknown), "unknown");
}

TEST(WorldbuildingSqliteHelpers, ExecPrepareStepResetBindsAndColumnsWork) {
    SqliteDb db(":memory:");
    db.exec("CREATE TABLE facts(name TEXT, count INTEGER, weight REAL)");

    Statement insert(
        db, "INSERT INTO facts(name, count, weight) VALUES(?1, ?2, ?3)");
    bind_text(insert, 1, "map");
    bind_int(insert, 2, 7);
    bind_double(insert, 3, 1.5);
    EXPECT_FALSE(insert.step());

    insert.reset();
    bind_text(insert, 1, "history");
    bind_int(insert, 2, 11);
    bind_double(insert, 3, 2.25);
    EXPECT_FALSE(insert.step());

    Statement query(db,
                    "SELECT name, count, weight FROM facts ORDER BY count ASC");
    ASSERT_TRUE(query.step());
    EXPECT_EQ(column_text(query, 0), "map");
    EXPECT_EQ(column_int(query, 1), 7);
    EXPECT_DOUBLE_EQ(column_double(query, 2), 1.5);
    ASSERT_TRUE(query.step());
    EXPECT_EQ(column_text(query, 0), "history");
    EXPECT_EQ(column_int(query, 1), 11);
    EXPECT_DOUBLE_EQ(column_double(query, 2), 2.25);
    EXPECT_FALSE(query.step());
}

TEST(WorldbuildingSqliteHelpers, MoveConstructionAndAssignmentTransferOwnership) {
    SqliteDb db(":memory:");
    db.exec("CREATE TABLE facts(value TEXT)");
    db.exec("INSERT INTO facts(value) VALUES('before move')");

    SqliteDb moved_db(std::move(db));
    moved_db.exec("INSERT INTO facts(value) VALUES('after construct')");

    SqliteDb assigned_db(":memory:");
    assigned_db = std::move(moved_db);
    assigned_db.exec("INSERT INTO facts(value) VALUES('after assign')");

    Statement query(assigned_db, "SELECT count(*) FROM facts");
    ASSERT_TRUE(query.step());
    EXPECT_EQ(column_int(query, 0), 3);

    Statement original_statement(assigned_db, "SELECT value FROM facts LIMIT 1");
    Statement moved_statement(std::move(original_statement));
    ASSERT_TRUE(moved_statement.step());
    EXPECT_EQ(column_text(moved_statement, 0), "before move");

    Statement assigned_statement(assigned_db, "SELECT 0");
    assigned_statement = Statement(assigned_db, "SELECT 42");
    ASSERT_TRUE(assigned_statement.step());
    EXPECT_EQ(column_int(assigned_statement, 0), 42);
}

TEST(WorldbuildingSqliteHelpers, MoveAssignmentDefersCloseWithLiveStatements) {
    SqliteDb db(":memory:");
    db.exec("CREATE TABLE facts(value INTEGER)");
    db.exec("INSERT INTO facts(value) VALUES(9)");
    Statement live_statement(db, "SELECT value FROM facts");

    SqliteDb replacement(":memory:");
    replacement.exec("CREATE TABLE other(value INTEGER)");
    replacement.exec("INSERT INTO other(value) VALUES(3)");

    db = std::move(replacement);

    ASSERT_TRUE(live_statement.step());
    EXPECT_EQ(column_int(live_statement, 0), 9);

    Statement replacement_query(db, "SELECT value FROM other");
    ASSERT_TRUE(replacement_query.step());
    EXPECT_EQ(column_int(replacement_query, 0), 3);
}

TEST(WorldbuildingSqliteHelpers, ErrorsIncludeSQLiteMessages) {
    SqliteDb db(":memory:");

    try {
        db.exec("INSERT INTO missing_table VALUES(1)");
        FAIL() << "expected missing table error";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("no such table"),
                  std::string::npos);
    }

    try {
        Statement bad_statement(db, "SELEC");
        FAIL() << "expected prepare error";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("syntax error"),
                  std::string::npos);
    }
}

TEST(WorldStore, CreateWorldCreatesAllDesignDirectoriesAndGodAgent) {
    WorldStore store(test_pg_conninfo(), temp_dir());
    store.initialize();
    auto world = store.create_world("北境", "雪原史诗");

    EXPECT_TRUE(std::filesystem::exists(store.world_path(world.id) /
                                        "world_knowledge"));
    EXPECT_TRUE(std::filesystem::exists(store.world_path(world.id) / "god"));
    EXPECT_TRUE(std::filesystem::exists(store.world_path(world.id) /
                                        "managers/map"));
    EXPECT_TRUE(std::filesystem::exists(store.world_path(world.id) /
                                        "managers/history"));
    EXPECT_TRUE(std::filesystem::exists(store.world_path(world.id) /
                                        "managers/magic"));
    EXPECT_TRUE(std::filesystem::exists(store.world_path(world.id) /
                                        "managers/faction"));
    EXPECT_TRUE(std::filesystem::exists(store.world_path(world.id) / "agents"));
    EXPECT_TRUE(std::filesystem::exists(store.world_path(world.id) / "scenes"));
    EXPECT_TRUE(std::filesystem::exists(store.world_path(world.id) /
                                        "chapters"));
    EXPECT_TRUE(std::filesystem::exists(store.world_path(world.id) / "arcs"));
    EXPECT_TRUE(std::filesystem::exists(store.world_path(world.id) / "secrets"));
    EXPECT_TRUE(std::filesystem::exists(store.world_path(world.id) /
                                        "foreshadows"));
    EXPECT_TRUE(std::filesystem::exists(store.world_path(world.id) /
                                        "sessions"));
    EXPECT_TRUE(std::filesystem::exists(store.world_path(world.id) /
                                        "timeline.json"));

    auto agents = store.list_agents(world.id);
    ASSERT_EQ(agents.size(), 2);
    EXPECT_EQ(agents[0].kind, AgentKind::God);
    EXPECT_EQ(agents[0].name, "god");
    EXPECT_EQ(agents[1].kind, AgentKind::RelationManager);
    EXPECT_EQ(agents[1].name, "relation_manager");
}

TEST(WorldStore, WorldsAreIsolated) {
    WorldStore store(test_pg_conninfo(), temp_dir());
    store.initialize();
    auto a = store.create_world("A", "");
    auto b = store.create_world("B", "");
    store.add_world_knowledge(a.id, {"", "history", "A only", ""});
    EXPECT_EQ(store.get_world_knowledge(a.id, "").size(), 1);
    EXPECT_TRUE(store.get_world_knowledge(b.id, "").empty());
}

TEST(WorldStore, GetAndListWorldsReturnPersistedMetadata) {
    WorldStore store(test_pg_conninfo(), temp_dir());
    store.initialize();

    auto a = store.create_world("A", "first");
    auto b = store.create_world("B", "second");

    auto loaded = store.get_world(a.id);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->id, a.id);
    EXPECT_EQ(loaded->name, "A");
    EXPECT_EQ(loaded->description, "first");

    auto worlds = store.list_worlds();
    ASSERT_EQ(worlds.size(), 2);
    EXPECT_EQ(worlds[0].id, a.id);
    EXPECT_EQ(worlds[1].id, b.id);
}

TEST(WorldStore, DeleteWorldRemovesRowsAndOnlyThatWorldDirectory) {
    auto root = temp_dir();
    WorldStore store(test_pg_conninfo(), root);
    store.initialize();
    auto a = store.create_world("A", "");
    auto b = store.create_world("B", "");
    store.add_world_knowledge(a.id, {"", "history", "A only", ""});
    const auto deleted_path = store.world_path(a.id);
    const auto kept_path = store.world_path(b.id);

    EXPECT_TRUE(store.delete_world(a.id));

    EXPECT_FALSE(store.get_world(a.id).has_value());
    EXPECT_TRUE(store.get_world(b.id).has_value());
    EXPECT_TRUE(store.get_world_knowledge(a.id, "").empty());
    EXPECT_TRUE(store.list_agents(a.id).empty());
    EXPECT_FALSE(std::filesystem::exists(deleted_path));
    EXPECT_TRUE(std::filesystem::exists(kept_path));
    EXPECT_FALSE(store.delete_world(a.id));
}

TEST(WorldStore, DatabaseEnforcesOneGodAgentPerWorld) {
    auto root = temp_dir();
    WorldStore store(test_pg_conninfo(), root);
    store.initialize();
    auto world = store.create_world("A", "");

    SqliteDb db((root / "worlds.sqlite3").string());
    Statement duplicate(db, R"sql(
        INSERT INTO agents(id, world_id, name, display_name, kind,
                           created_at, updated_at)
        VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)
    )sql");
    bind_text(duplicate, 1, make_id("agent"));
    bind_text(duplicate, 2, world.id);
    bind_text(duplicate, 3, "second-god");
    bind_text(duplicate, 4, "second-god");
    bind_text(duplicate, 5, "god");
    bind_text(duplicate, 6, now_iso_utc());
    bind_text(duplicate, 7, now_iso_utc());

    EXPECT_THROW(duplicate.step(), std::runtime_error);
}

TEST(WorldStore, ListAgentsThrowsForUnknownPersistedKind) {
    auto root = temp_dir();
    WorldStore store(test_pg_conninfo(), root);
    store.initialize();
    auto world = store.create_world("A", "");

    SqliteDb db((root / "worlds.sqlite3").string());
    Statement corrupt(db, "UPDATE agents SET kind = ?1 WHERE world_id = ?2");
    bind_text(corrupt, 1, "mystery");
    bind_text(corrupt, 2, world.id);
    EXPECT_FALSE(corrupt.step());

    EXPECT_THROW(store.list_agents(world.id), std::runtime_error);
}
