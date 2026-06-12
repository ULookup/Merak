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
    src/ask_user_tool.cpp
    src/web_search_tool.cpp
    src/web_fetch_tool.cpp
    src/symbols_tool.cpp
    src/session_tool.cpp
    src/task_tool.cpp
    src/plan_mode_tools.cpp
    src/lsp_tool.cpp
)
target_include_directories(merak-tools PUBLIC include)
target_link_libraries(merak-tools PUBLIC
    merak-core
    merak-mcp
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    httplib::httplib
)
