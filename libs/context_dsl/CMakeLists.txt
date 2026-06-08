add_library(merak-context-dsl STATIC
    src/parser.cpp
    src/resolver.cpp
    src/renderer.cpp
)
target_include_directories(merak-context-dsl PUBLIC include)
target_link_libraries(merak-context-dsl PUBLIC
    merak-worldbuilding
    nlohmann_json::nlohmann_json
)
