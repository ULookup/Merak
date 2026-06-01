add_library(merak-context STATIC
    src/token_counter.cpp
    src/context_assembler.cpp
    src/compactor.cpp
    src/cache_aware_context.cpp
)
target_include_directories(merak-context PUBLIC include)
target_link_libraries(merak-context PUBLIC
    merak-core
    merak-memory
    merak-llm
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
