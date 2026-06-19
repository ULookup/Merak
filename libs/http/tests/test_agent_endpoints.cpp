#include <gtest/gtest.h>
#include <merak/worldbuilding/world_models.hpp>
#include <merak/worldbuilding/worldbuilding_service.hpp>
#include <merak/worldbuilding/agent_store.hpp>

#include "../../worldbuilding/tests/test_helpers.hpp"

#include <filesystem>
#include <string>
#include <vector>

using namespace merak::worldbuilding;
using namespace merak::worldbuilding::test;

namespace {

std::filesystem::path temp_dir() {
    auto path = std::filesystem::temp_directory_path() / ("agent_endpoint_test_" + std::to_string(std::rand()));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

} // namespace

TEST(AgentSearch, SearchByQueryMatchesNameAndDisplayName) {
    WorldbuildingService svc(test_pg_conninfo(), temp_dir());
    svc.initialize();
    auto world = svc.create_world("SearchTest", "test");

    CharacterCard card1; card1.name = "BraveKnight";
    card1.background = "Strongest warrior in the kingdom"; card1.race = "Human"; card1.identity = "Knight";
    svc.create_character(world.id, card1);

    CharacterCard card2; card2.name = "EvilMage";
    card2.background = "Dark forest hermit"; card2.race = "Elf"; card2.identity = "Mage";
    svc.create_character(world.id, card2);

    AgentStore::SearchCriteria criteria;
    criteria.q = "Knight";
    auto results = svc.agents().search_agents(world.id, criteria);
    ASSERT_GE(results.size(), 1);
    EXPECT_EQ(results[0].name, "BraveKnight");
}

TEST(AgentSearch, SearchByTraitsFiltersCorrectly) {
    WorldbuildingService svc(test_pg_conninfo(), temp_dir());
    svc.initialize();
    auto world = svc.create_world("TraitSearch", "test");

    CharacterCard card; card.name = "LoyalGuard";
    card.core_traits = {"loyal", "brave"};
    card.race = "Human";
    svc.create_character(world.id, card);

    AgentStore::SearchCriteria criteria;
    criteria.traits = {"loyal"};
    auto results = svc.agents().search_agents(world.id, criteria);
    ASSERT_GE(results.size(), 1);
    EXPECT_EQ(results[0].name, "LoyalGuard");

    criteria.traits = {"nonexistent_trait"};
    auto empty = svc.agents().search_agents(world.id, criteria);
    EXPECT_EQ(empty.size(), 0);
}

TEST(AgentSearch, SearchByRaceAndIdentity) {
    WorldbuildingService svc(test_pg_conninfo(), temp_dir());
    svc.initialize();
    auto world = svc.create_world("RaceSearch", "test");

    CharacterCard card; card.name = "ElfArcher";
    card.race = "Elf"; card.identity = "Ranger";
    svc.create_character(world.id, card);

    AgentStore::SearchCriteria criteria;
    criteria.race = "Elf";
    auto results = svc.agents().search_agents(world.id, criteria);
    ASSERT_GE(results.size(), 1);

    criteria.race = "Orc";
    auto empty = svc.agents().search_agents(world.id, criteria);
    EXPECT_EQ(empty.size(), 0);
}

TEST(AgentList, KindFilterReturnsOnlyMatchingKind) {
    WorldbuildingService svc(test_pg_conninfo(), temp_dir());
    svc.initialize();
    auto world = svc.create_world("KindFilter", "test");

    auto agents = svc.worlds().list_agents(world.id, std::string{"individual"});
    for (const auto& a : agents) {
        EXPECT_EQ(a.kind, AgentKind::Individual);
    }
}

TEST(CharacterAppearances, FindAppearancesReturnsSceneParticipation) {
    WorldbuildingService svc(test_pg_conninfo(), temp_dir());
    svc.initialize();
    auto world = svc.create_world("AppearanceTest", "test");

    CharacterCard card; card.name = "Hero";
    auto agent = svc.create_character(world.id, card);

    Chapter ch; ch.title = "Chapter One"; ch.number = 1;
    auto chapter = svc.create_chapter(world.id, ch);

    Scene scene; scene.title = "Hero Enters"; scene.chapter_id = chapter.id;
    scene.participant_ids = {agent.id};
    auto created_scene = svc.create_scene(world.id, scene);

    auto appearances = svc.narrative().find_character_appearances(world.id, agent.id);
    ASSERT_GE(appearances.scenes.size(), 1);
    bool found = false;
    for (const auto& sc : appearances.scenes) {
        if (sc["id"] == created_scene.id) { found = true; break; }
    }
    EXPECT_TRUE(found);
}
