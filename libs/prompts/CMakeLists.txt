add_library(merak-prompts STATIC
    src/core_prompt.cpp
    src/team_prompt.cpp
)
target_include_directories(merak-prompts PUBLIC include)
target_link_libraries(merak-prompts PUBLIC
    merak-core
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
