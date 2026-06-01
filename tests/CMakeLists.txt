enable_testing()

# 核心类型单元测试
add_executable(merak-core-test
    ${CMAKE_SOURCE_DIR}/libs/core/tests/test_compile.cpp
)
target_link_libraries(merak-core-test PRIVATE merak-core)

# 工具系统测试
add_executable(merak-tools-test
    ${CMAKE_SOURCE_DIR}/libs/tools/tests/test_tools.cpp
)
target_link_libraries(merak-tools-test PRIVATE merak-tools)

# 上下文测试
add_executable(merak-context-test
    ${CMAKE_SOURCE_DIR}/libs/context/tests/test_context.cpp
)
target_link_libraries(merak-context-test PRIVATE merak-context)
