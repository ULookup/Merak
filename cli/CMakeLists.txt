include(FetchContent)

FetchContent_Declare(
    tree-sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter
    GIT_TAG v0.22.6
)
FetchContent_MakeAvailable(tree-sitter)

set(TS_LANGUAGES cpp python go javascript typescript rust bash json yaml markdown sql java csharp ruby)

foreach(lang IN LISTS TS_LANGUAGES)
    FetchContent_Declare(
        tree-sitter-${lang}
        GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-${lang}
        GIT_TAG master
    )
    FetchContent_MakeAvailable(tree-sitter-${lang})
endforeach()

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
    tree-sitter ${TS_LANGUAGES}
)
target_include_directories(merak-cli PRIVATE src)

add_executable(test_buffer tests/test_buffer.cpp)
target_include_directories(test_buffer PRIVATE src)
target_compile_features(test_buffer PRIVATE cxx_std_17)
add_test(NAME test_buffer COMMAND test_buffer)

add_executable(test_syntax tests/test_syntax.cpp)
target_link_libraries(test_syntax PRIVATE tree-sitter tree-sitter-python)
target_include_directories(test_syntax PRIVATE src)
target_compile_features(test_syntax PRIVATE cxx_std_17)
add_test(NAME test_syntax COMMAND test_syntax)

add_executable(test_persistence tests/test_persistence.cpp)
target_link_libraries(test_persistence PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(test_persistence PRIVATE src)
target_compile_features(test_persistence PRIVATE cxx_std_17)
add_test(NAME test_persistence COMMAND test_persistence)

install(TARGETS merak-cli RUNTIME DESTINATION bin)
