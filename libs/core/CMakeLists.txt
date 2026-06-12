# merak-core: shared types + utilities (json_rpc_client, etc.)
add_library(merak-core STATIC
    src/json_rpc_client.cpp
)
target_include_directories(merak-core PUBLIC include)
target_link_libraries(merak-core PUBLIC nlohmann_json::nlohmann_json)

# 编译时验证头文件可独立编译
add_library(merak-core-test-compile STATIC
    tests/test_compile.cpp
)
target_link_libraries(merak-core-test-compile PRIVATE merak-core)
