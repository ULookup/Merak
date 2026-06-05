add_executable(merak-cli
    src/main.cpp
    src/theme/theme.cpp
    src/output/cli_output.cpp
    src/commands/command_registry.cpp
    src/commands/worldbuilding_commands.cpp
    src/client/runtime_client.cpp
)
set_target_properties(merak-cli PROPERTIES OUTPUT_NAME merak)
target_link_libraries(merak-cli PRIVATE
    merak-http merak-runtime merak-storage merak-loop merak-config
    merak-llm merak-tools merak-mcp merak-memory merak-context
    CURL::libcurl
)
target_include_directories(merak-cli PRIVATE src)

add_executable(test_buffer tests/test_buffer.cpp)
target_include_directories(test_buffer PRIVATE src)
target_compile_features(test_buffer PRIVATE cxx_std_17)
add_test(NAME test_buffer COMMAND test_buffer)

install(TARGETS merak-cli RUNTIME DESTINATION bin)
