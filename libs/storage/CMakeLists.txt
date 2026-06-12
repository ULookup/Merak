add_library(merak-storage STATIC
    src/session_store.cpp
    src/session_store_pg.cpp
    src/local_file_image_store.cpp
    src/image_service.cpp
)
target_include_directories(merak-storage PUBLIC include)
target_link_libraries(merak-storage PUBLIC
    merak-core
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
    libpqxx::pqxx
)
