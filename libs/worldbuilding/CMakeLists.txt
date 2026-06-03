add_library(merak-worldbuilding STATIC
    src/ids.cpp
    src/agent_store.cpp
    src/foreshadowing_store.cpp
    src/narrative_store.cpp
    src/secret_store.cpp
    src/sqlite_helpers.cpp
    src/world_store.cpp
    src/voice_analyzer.cpp
    src/scene_orchestrator.cpp
    src/worldbuilding_service.cpp
)
target_include_directories(merak-worldbuilding PUBLIC include)
target_link_libraries(merak-worldbuilding PUBLIC
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
)

add_executable(worldbuilding_tests
    tests/test_agent_store.cpp
    tests/test_foreshadowing_secret.cpp
    tests/test_narrative_store.cpp
    tests/test_scene_orchestrator.cpp
    tests/test_voice_analyzer.cpp
    tests/test_world_store.cpp
    tests/test_worldbuilding_e2e.cpp
)
target_link_libraries(worldbuilding_tests PRIVATE
    merak-worldbuilding
    GTest::gtest_main
)
add_test(NAME worldbuilding_tests COMMAND worldbuilding_tests)
