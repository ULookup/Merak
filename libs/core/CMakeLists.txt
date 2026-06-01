# merak-core: 仅头文件，定义所有共享类型
add_library(merak-core INTERFACE)
target_include_directories(merak-core INTERFACE include)
target_link_libraries(merak-core INTERFACE nlohmann_json::nlohmann_json)

# 编译时验证头文件可独立编译
add_library(merak-core-test-compile STATIC
    tests/test_compile.cpp
)
target_link_libraries(merak-core-test-compile PRIVATE merak-core)
