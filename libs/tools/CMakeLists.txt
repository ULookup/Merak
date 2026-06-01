add_library(merak-tools STATIC
    src/tool_registry.cpp
    src/builtin_tools.cpp
)
target_include_directories(merak-tools PUBLIC include)
target_link_libraries(merak-tools PUBLIC
    merak-core
    merak-mcp
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
