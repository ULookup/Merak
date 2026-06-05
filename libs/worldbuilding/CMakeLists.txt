find_package(PostgreSQL REQUIRED)
find_path(LIBPQ_INCLUDE_DIR libpq-fe.h REQUIRED)

add_library(merak-worldbuilding STATIC
    src/ids.cpp
    src/agent_store.cpp
    src/foreshadowing_store.cpp
    src/narrative_store.cpp
    src/secret_store.cpp
    src/pg_helpers.cpp
    src/sqlite_helpers.cpp
    src/world_store.cpp
    src/voice_analyzer.cpp
    src/scene_orchestrator.cpp
    src/worldbuilding_service.cpp
    src/worldbuilding_tools.cpp
)
target_include_directories(merak-worldbuilding PUBLIC
    include
    ${PostgreSQL_INCLUDE_DIRS}
    ${LIBPQ_INCLUDE_DIR}
)
target_link_libraries(merak-worldbuilding PUBLIC
    merak-tools
    nlohmann_json::nlohmann_json
    ${PostgreSQL_LIBRARIES}
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
    SQLite::SQLite3
)
add_test(NAME worldbuilding_tests COMMAND worldbuilding_tests)
