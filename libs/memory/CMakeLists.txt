add_library(merak-memory STATIC
    src/memory_store.cpp
)
target_include_directories(merak-memory PUBLIC include)
target_link_libraries(merak-memory PUBLIC
    merak-core
    merak-config
    libpqxx::pqxx
    spdlog::spdlog
)
