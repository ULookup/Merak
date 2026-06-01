add_library(merak-mcp STATIC
    src/mcp_client.cpp
)
target_include_directories(merak-mcp PUBLIC include)
target_link_libraries(merak-mcp PUBLIC
    merak-core
    merak-config
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
