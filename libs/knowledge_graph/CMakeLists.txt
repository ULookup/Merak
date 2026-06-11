find_package(neo4j-client REQUIRED)

add_library(merak-knowledge-graph STATIC
    src/neo4j_provider.cpp
)
target_include_directories(merak-knowledge-graph PUBLIC
    include
    ${neo4j-client_INCLUDE_DIRS}
)
target_link_libraries(merak-knowledge-graph PUBLIC
    neo4j-client::neo4j-client
    nlohmann_json::nlohmann_json
)

# Tests
add_executable(kg_tests
    tests/test_kg_models.cpp
    tests/test_kg_provider_mock.cpp
)
target_link_libraries(kg_tests PRIVATE
    merak-knowledge-graph
    GTest::gtest_main
)
add_test(NAME kg_tests COMMAND kg_tests)

# Integration tests (require Neo4j Docker)
add_executable(kg_integration_tests
    tests/test_kg_integration.cpp
)
target_link_libraries(kg_integration_tests PRIVATE
    merak-knowledge-graph
    GTest::gtest_main
)
