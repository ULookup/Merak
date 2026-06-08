add_library(merak-prompts STATIC
    src/compositor.cpp
    src/core_prompt.cpp
    src/memory_prompt.cpp
    src/scene_prompt.cpp
    src/skill_prompt.cpp
    src/team_prompt.cpp
)
target_include_directories(merak-prompts PUBLIC include)
target_link_libraries(merak-prompts PUBLIC
    merak-core
    merak-context-dsl
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

add_executable(test-prompts tests/test_prompts.cpp)
target_link_libraries(test-prompts PRIVATE
    merak-prompts
    GTest::gtest_main
)
add_test(NAME prompts COMMAND test-prompts WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
