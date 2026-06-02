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

# TUI model tests
add_executable(merak-tui-test
    ${CMAKE_SOURCE_DIR}/cli/tests/test_tui_model.cpp
)
target_include_directories(merak-tui-test PRIVATE ${CMAKE_SOURCE_DIR}/cli/src)
target_link_libraries(merak-tui-test PRIVATE merak-core ftxui::component nlohmann_json::nlohmann_json)
add_test(NAME merak-tui-test COMMAND merak-tui-test)

add_executable(merak-loop-events-test
    ${CMAKE_SOURCE_DIR}/libs/loop/tests/test_loop_events.cpp
)
target_link_libraries(merak-loop-events-test PRIVATE merak-loop)
add_test(NAME merak-loop-events-test COMMAND merak-loop-events-test)
