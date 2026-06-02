add_executable(merak-cli
    src/main.cpp
    src/theme/theme.cpp
    src/output/cli_output.cpp
    src/commands/command_registry.cpp
    src/client/runtime_client.cpp
)
set_target_properties(merak-cli PROPERTIES OUTPUT_NAME merak)
target_link_libraries(merak-cli PRIVATE
    merak-http merak-runtime merak-storage merak-loop merak-config
    merak-llm merak-tools merak-mcp merak-memory merak-context
    ftxui::component CURL::libcurl
)
target_include_directories(merak-cli PRIVATE src)

install(TARGETS merak-cli RUNTIME DESTINATION bin)
