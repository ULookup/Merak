add_library(merak-runtime STATIC
    src/runtime_service.cpp
)
target_include_directories(merak-runtime PUBLIC include)
target_link_libraries(merak-runtime PUBLIC
    merak-core
    merak-storage
    merak-loop
    merak-worldbuilding
    nlohmann_json::nlohmann_json
)
