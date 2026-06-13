add_library(merak-memory STATIC
    src/memory_store.cpp
    src/narrative_working_memory.cpp
    src/session_journal.cpp
    src/memory_extraction_service.cpp
    src/openai_embedding_provider.cpp
)
target_include_directories(merak-memory PUBLIC include)
target_link_libraries(merak-memory PUBLIC
    merak-core
    merak-config
    merak-llm
    libpqxx::pqxx
    spdlog::spdlog
    httplib::httplib
    CURL::libcurl
    OpenSSL::SSL
)
