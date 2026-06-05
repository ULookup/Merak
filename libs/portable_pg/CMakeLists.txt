add_library(merak-portable-pg STATIC
    src/portable_pg.cpp
)
target_include_directories(merak-portable-pg PUBLIC include)
target_link_libraries(merak-portable-pg PUBLIC merak-core)
