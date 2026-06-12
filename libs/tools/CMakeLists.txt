add_library(merak-tools STATIC
    src/edit_journal.cpp
    src/tool_registry.cpp
    src/fs_tools.cpp
    src/search_tools.cpp
    src/shell_tool.cpp
    src/tool_search_tool.cpp
    src/git_tool.cpp
    src/memory_tool.cpp
    src/agent_tool.cpp
)
target_include_directories(merak-tools PUBLIC include)
target_link_libraries(merak-tools PUBLIC
    merak-core
    merak-mcp
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
