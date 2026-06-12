add_library(merak-context STATIC
    src/token_counter.cpp
    src/pipeline_stats.cpp
    src/context_assembler.cpp
    src/compactor.cpp
    src/cache_aware_context.cpp
    src/context_optimizer.cpp
    src/context_planner.cpp
    src/context_binder.cpp
    src/context_serializer.cpp
    src/context_pipeline.cpp
    src/spill_store.cpp
)
target_include_directories(merak-context PUBLIC include)
target_link_libraries(merak-context PUBLIC
    merak-core
    merak-memory
    merak-llm
    merak-prompts
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
