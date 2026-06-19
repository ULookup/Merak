include(FetchContent)

FetchContent_Declare(
    tree-sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter
    GIT_TAG v0.22.6
)
FetchContent_GetProperties(tree-sitter)
if(NOT tree-sitter_POPULATED)
    FetchContent_Populate(tree-sitter)
endif()

# tree-sitter v0.22+ has no CMakeLists.txt (Rust project).
# Build the C library manually with WASM stubs since we use compiled-in grammars.
enable_language(C)
if(NOT TARGET tree-sitter)
    # WASM stubs — grammar parser.c files reference these symbols, but we
    # never load WASM grammars, so provide no-op implementations.
    set(TS_WASM_STUB "${CMAKE_CURRENT_BINARY_DIR}/wasm_stubs.c")
    file(WRITE ${TS_WASM_STUB} [=[
        #include <tree_sitter/api.h>
        #include <stdint.h>
        typedef uint16_t TSStateId;
        typedef struct TSLexer TSLexer;
        // wasm_store stubs — grammars compiled as C never use WASM,
        // but parser.c and language.c reference these symbols.
        bool ts_language_is_wasm(const TSLanguage *self) { (void)self; return false; }
        void ts_wasm_store_delete(TSWasmStore *self) { (void)self; }
        bool ts_wasm_store_start(TSWasmStore *s, TSLexer *l, const TSLanguage *lang) {
            (void)s; (void)l; (void)lang; return false;
        }
        void ts_wasm_store_reset(TSWasmStore *self) { (void)self; }
        bool ts_wasm_store_has_error(const TSWasmStore *self) { (void)self; return false; }
        bool ts_wasm_store_call_lex_main(TSWasmStore *s, TSStateId id) { (void)s; (void)id; return false; }
        bool ts_wasm_store_call_lex_keyword(TSWasmStore *s, TSStateId id) { (void)s; (void)id; return false; }
        uint32_t ts_wasm_store_call_scanner_create(TSWasmStore *s) { (void)s; return 0; }
        void ts_wasm_store_call_scanner_destroy(TSWasmStore *s, uint32_t idx) { (void)s; (void)idx; }
        bool ts_wasm_store_call_scanner_scan(TSWasmStore *s, uint32_t idx, uint32_t state) { (void)s; (void)idx; (void)state; return false; }
        uint32_t ts_wasm_store_call_scanner_serialize(TSWasmStore *s, uint32_t idx, char *buf) { (void)s; (void)idx; (void)buf; return 0; }
        void ts_wasm_store_call_scanner_deserialize(TSWasmStore *s, uint32_t idx, const char *buf, unsigned len) { (void)s; (void)idx; (void)buf; (void)len; }
        void ts_wasm_language_retain(const TSLanguage *self) { (void)self; }
        void ts_wasm_language_release(const TSLanguage *self) { (void)self; }
    ]=])
    file(GLOB TS_SOURCES "${tree-sitter_SOURCE_DIR}/lib/src/*.c")
    list(FILTER TS_SOURCES EXCLUDE REGEX ".*/lib\\.c$")
    list(FILTER TS_SOURCES EXCLUDE REGEX ".*/wasm_store\\.c$")
    add_library(tree-sitter STATIC ${TS_SOURCES} ${TS_WASM_STUB})
    target_include_directories(tree-sitter PUBLIC
        ${tree-sitter_SOURCE_DIR}/lib/include
    )
    target_include_directories(tree-sitter PRIVATE
        ${tree-sitter_SOURCE_DIR}/lib/src
    )
    target_compile_features(tree-sitter PRIVATE c_std_11)
    set_target_properties(tree-sitter PROPERTIES LINKER_LANGUAGE C)
endif()

set(TS_LANGUAGES cpp python go javascript typescript rust bash json yaml markdown sql java csharp ruby)

# Map language → repo URL + branch + source subdirectory.
# Default repo:  tree-sitter/tree-sitter-${lang}
# Default branch: master
# Default src:   src/
foreach(lang IN LISTS TS_LANGUAGES)
    if(lang STREQUAL "typescript")
        set(TS_REPO_URL "https://github.com/tree-sitter/tree-sitter-typescript")
        set(TS_GIT_TAG "master")
        set(TS_SRC_SUBDIR "typescript/src")
    elseif(lang STREQUAL "csharp")
        set(TS_REPO_URL "https://github.com/tree-sitter/tree-sitter-c-sharp")
        set(TS_GIT_TAG "master")
        set(TS_SRC_SUBDIR "src")
    elseif(lang STREQUAL "yaml")
        set(TS_REPO_URL "https://github.com/ikatyang/tree-sitter-yaml")
        set(TS_GIT_TAG "master")
        set(TS_SRC_SUBDIR "src")
    elseif(lang STREQUAL "markdown")
        set(TS_REPO_URL "https://github.com/tree-sitter-grammars/tree-sitter-markdown")
        set(TS_GIT_TAG "split_parser")
        set(TS_SRC_SUBDIR "tree-sitter-markdown/src")
    elseif(lang STREQUAL "sql")
        set(TS_REPO_URL "https://github.com/m-novikov/tree-sitter-sql")
        set(TS_GIT_TAG "main")
        set(TS_SRC_SUBDIR "src")
    else()
        set(TS_REPO_URL "https://github.com/tree-sitter/tree-sitter-${lang}")
        set(TS_GIT_TAG "master")
        set(TS_SRC_SUBDIR "src")
    endif()

    FetchContent_Declare(
        tree-sitter-${lang}
        GIT_REPOSITORY ${TS_REPO_URL}
        GIT_TAG ${TS_GIT_TAG}
    )
    FetchContent_GetProperties(tree-sitter-${lang})
    if(NOT tree-sitter-${lang}_POPULATED)
        FetchContent_Populate(tree-sitter-${lang})
    endif()
    if(NOT TARGET tree-sitter-${lang})
        add_library(tree-sitter-${lang} STATIC
            ${tree-sitter-${lang}_SOURCE_DIR}/${TS_SRC_SUBDIR}/parser.c
        )
        set(TS_LINK_LANG C)
        if(EXISTS ${tree-sitter-${lang}_SOURCE_DIR}/${TS_SRC_SUBDIR}/scanner.c)
            target_sources(tree-sitter-${lang} PRIVATE
                ${tree-sitter-${lang}_SOURCE_DIR}/${TS_SRC_SUBDIR}/scanner.c
            )
        elseif(EXISTS ${tree-sitter-${lang}_SOURCE_DIR}/${TS_SRC_SUBDIR}/scanner.cc)
            target_sources(tree-sitter-${lang} PRIVATE
                ${tree-sitter-${lang}_SOURCE_DIR}/${TS_SRC_SUBDIR}/scanner.cc
            )
            set_source_files_properties(
                ${tree-sitter-${lang}_SOURCE_DIR}/${TS_SRC_SUBDIR}/scanner.cc
                PROPERTIES LANGUAGE CXX
            )
            set(TS_LINK_LANG CXX)
        endif()
        target_include_directories(tree-sitter-${lang} PRIVATE
            ${tree-sitter-${lang}_SOURCE_DIR}/${TS_SRC_SUBDIR}
        )
        set_target_properties(tree-sitter-${lang} PROPERTIES LINKER_LANGUAGE ${TS_LINK_LANG})
        target_link_libraries(tree-sitter-${lang} PRIVATE tree-sitter)
    endif()
endforeach()

# Map language names to CMake target names: "cpp" → "tree-sitter-cpp"
set(TS_GRAMMAR_TARGETS "")
foreach(lang IN LISTS TS_LANGUAGES)
    list(APPEND TS_GRAMMAR_TARGETS "tree-sitter-${lang}")
endforeach()

add_executable(merak-cli
    src/main.cpp
    src/theme/theme.cpp
    src/output/cli_output.cpp
    src/commands/command_registry.cpp
    src/commands/worldbuilding_commands.cpp
    src/client/runtime_client.cpp
    src/tui/tui_runner.cpp
)
set_target_properties(merak-cli PROPERTIES OUTPUT_NAME merak)
target_link_libraries(merak-cli PRIVATE
    merak-app merak-http merak-runtime merak-storage merak-loop merak-config
    merak-llm merak-tools merak-mcp merak-memory merak-context
    merak-portable-pg
    CURL::libcurl
    tree-sitter ${TS_GRAMMAR_TARGETS}
)
target_include_directories(merak-cli PRIVATE src)

add_executable(test_buffer tests/test_buffer.cpp)
target_include_directories(test_buffer PRIVATE src)
target_compile_features(test_buffer PRIVATE cxx_std_23)
add_test(NAME test_buffer COMMAND test_buffer)

add_executable(test_syntax tests/test_syntax.cpp)
target_link_libraries(test_syntax PRIVATE tree-sitter ${TS_GRAMMAR_TARGETS} nlohmann_json::nlohmann_json)
target_include_directories(test_syntax PRIVATE
    src
    ${tree-sitter_SOURCE_DIR}/lib/include
)
target_compile_features(test_syntax PRIVATE cxx_std_23)
add_test(NAME test_syntax COMMAND test_syntax)

add_executable(test_persistence tests/test_persistence.cpp)
target_link_libraries(test_persistence PRIVATE nlohmann_json::nlohmann_json)
target_include_directories(test_persistence PRIVATE src)
target_compile_features(test_persistence PRIVATE cxx_std_23)
add_test(NAME test_persistence COMMAND test_persistence)

install(TARGETS merak-cli RUNTIME DESTINATION bin)

if(WIN32)
    add_executable(merak-launcher src/win_launcher.cpp)
    target_compile_features(merak-launcher PRIVATE cxx_std_23)
    target_link_libraries(merak-launcher PRIVATE ws2_32 shell32 user32)
    set_target_properties(merak-launcher PROPERTIES
        RUNTIME_OUTPUT_NAME "merak-launcher"
        WIN32_EXECUTABLE TRUE
    )
endif()
