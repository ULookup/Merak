add_library(merak-config STATIC
    src/config_loader.cpp
)
target_include_directories(merak-config PUBLIC include)
target_link_libraries(merak-config PUBLIC merak-core nlohmann_json::nlohmann_json)

# 配置加载器测试
add_executable(merak-config-test tests/test_config.cpp)
target_link_libraries(merak-config-test PRIVATE merak-config)
