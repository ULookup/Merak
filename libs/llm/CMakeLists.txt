add_library(merak-llm STATIC
    src/openai_provider.cpp
    src/anthropic_provider.cpp
)
target_include_directories(merak-llm PUBLIC include)
target_link_libraries(merak-llm PUBLIC
    merak-core
    merak-config
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    CURL::libcurl
)
