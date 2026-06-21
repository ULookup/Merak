enable_testing()

# 核心类型单元测试
add_executable(merak-core-test
    ${CMAKE_SOURCE_DIR}/libs/core/tests/test_compile.cpp
)
target_link_libraries(merak-core-test PRIVATE merak-core)
add_test(NAME merak-core-test COMMAND merak-core-test)

# 工具系统测试
add_executable(merak-tools-test
    ${CMAKE_SOURCE_DIR}/libs/tools/tests/test_tools.cpp
)
target_link_libraries(merak-tools-test PRIVATE merak-tools)
add_test(NAME merak-tools-test COMMAND merak-tools-test)

# 上下文测试
add_executable(merak-context-test
    ${CMAKE_SOURCE_DIR}/libs/context/tests/test_context.cpp
)
target_link_libraries(merak-context-test PRIVATE merak-context)
add_test(NAME merak-context-test COMMAND merak-context-test)

add_executable(merak-storage-test
    ${CMAKE_SOURCE_DIR}/libs/storage/tests/test_storage.cpp
)
target_link_libraries(merak-storage-test PRIVATE merak-storage)
add_test(NAME merak-storage-test COMMAND merak-storage-test)

add_executable(merak-runtime-test
    ${CMAKE_SOURCE_DIR}/libs/runtime/tests/test_runtime.cpp
)
target_link_libraries(merak-runtime-test PRIVATE merak-runtime)
add_test(NAME merak-runtime-test COMMAND merak-runtime-test)

add_executable(merak-http-test
    ${CMAKE_SOURCE_DIR}/libs/http/tests/test_http.cpp
)
target_link_libraries(merak-http-test PRIVATE merak-http)
add_test(NAME merak-http-test COMMAND merak-http-test)

add_executable(merak-tui-client-test
    ${CMAKE_SOURCE_DIR}/cli/tests/test_runtime_client.cpp
    ${CMAKE_SOURCE_DIR}/cli/src/client/runtime_client.cpp
)
target_include_directories(merak-tui-client-test PRIVATE ${CMAKE_SOURCE_DIR}/cli/src)
target_link_libraries(merak-tui-client-test PRIVATE merak-core CURL::libcurl)
add_test(NAME merak-tui-client-test COMMAND merak-tui-client-test)

add_executable(merak-tui-features-test
    ${CMAKE_SOURCE_DIR}/cli/tests/test_tui_features.cpp
    ${CMAKE_SOURCE_DIR}/cli/src/theme/theme.cpp
    ${CMAKE_SOURCE_DIR}/cli/src/commands/command_registry.cpp
)
target_include_directories(merak-tui-features-test PRIVATE
    ${CMAKE_SOURCE_DIR}/cli/src
    ${tree-sitter_SOURCE_DIR}/lib/include
)
target_link_libraries(merak-tui-features-test PRIVATE merak-core ftxui::component tree-sitter)
add_test(NAME merak-tui-features-test COMMAND merak-tui-features-test)

# Agent endpoint integration tests
add_executable(merak-agent-endpoint-test
    ${CMAKE_SOURCE_DIR}/libs/http/tests/test_agent_endpoints.cpp
)
target_include_directories(merak-agent-endpoint-test PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/worldbuilding/tests
)
target_link_libraries(merak-agent-endpoint-test PRIVATE merak-worldbuilding GTest::gtest_main)
add_test(NAME merak-agent-endpoint-test COMMAND merak-agent-endpoint-test)

# OpenAI provider tests
add_executable(merak-openai-provider-test
    ${CMAKE_SOURCE_DIR}/libs/llm/tests/test_openai_provider.cpp
)
target_link_libraries(merak-openai-provider-test PRIVATE merak-llm)
add_test(NAME merak-openai-provider-test COMMAND merak-openai-provider-test)

# Anthropic provider tests
add_executable(merak-anthropic-provider-test
    ${CMAKE_SOURCE_DIR}/libs/llm/tests/test_anthropic_provider.cpp
)
target_link_libraries(merak-anthropic-provider-test PRIVATE merak-llm)
add_test(NAME merak-anthropic-provider-test COMMAND merak-anthropic-provider-test)

# AgentLoop tests
add_executable(merak-agent-loop-test
    ${CMAKE_SOURCE_DIR}/libs/loop/tests/test_agent_loop.cpp
)
target_link_libraries(merak-agent-loop-test PRIVATE merak-loop)
add_test(NAME merak-agent-loop-test COMMAND merak-agent-loop-test)

# TurnGuard tests
add_executable(merak-turn-guard-test
    ${CMAKE_SOURCE_DIR}/libs/loop/tests/test_turn_guard.cpp
)
target_link_libraries(merak-turn-guard-test PRIVATE merak-loop)
add_test(NAME merak-turn-guard-test COMMAND merak-turn-guard-test)

# SubAgentRunner tests
add_executable(merak-sub-agent-runner-test
    ${CMAKE_SOURCE_DIR}/libs/loop/tests/test_sub_agent_runner.cpp
)
target_link_libraries(merak-sub-agent-runner-test PRIVATE merak-loop)
add_test(NAME merak-sub-agent-runner-test COMMAND merak-sub-agent-runner-test)
