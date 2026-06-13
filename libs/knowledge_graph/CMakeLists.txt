find_package(PkgConfig)
pkg_check_modules(NEO4J_CLIENT IMPORTED_TARGET libneo4j-client)

if(NOT NEO4J_CLIENT_FOUND)
  message(WARNING "libneo4j-client not found via pkg-config, trying manual search")
  find_path(NEO4J_CLIENT_INCLUDE_DIR neo4j-client.h)
  find_library(NEO4J_CLIENT_LIBRARY neo4j-client)
  if(NEO4J_CLIENT_INCLUDE_DIR AND NEO4J_CLIENT_LIBRARY)
    add_library(neo4j-client::neo4j-client UNKNOWN IMPORTED)
    set_target_properties(neo4j-client::neo4j-client PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${NEO4J_CLIENT_INCLUDE_DIR}"
      IMPORTED_LOCATION "${NEO4J_CLIENT_LIBRARY}")
    set(NEO4J_CLIENT_FOUND TRUE)
  endif()
endif()

if(NOT NEO4J_CLIENT_FOUND)
  message(WARNING "libneo4j-client not found — skipping merak-knowledge-graph")
  return()
endif()

add_library(merak-knowledge-graph STATIC
    src/neo4j_provider.cpp
)
target_include_directories(merak-knowledge-graph PUBLIC
    include
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
