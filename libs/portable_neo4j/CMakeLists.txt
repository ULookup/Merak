add_library(merak-portable-neo4j STATIC
    src/portable_neo4j.cpp
)
target_include_directories(merak-portable-neo4j PUBLIC include)
target_link_libraries(merak-portable-neo4j PUBLIC merak-core)
