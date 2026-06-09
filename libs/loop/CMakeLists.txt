add_library(merak-loop STATIC
    src/agent_loop.cpp
    src/sub_agent_runner.cpp
    src/stall_detector.cpp
    src/turn_ingestor.cpp
    src/turn_guard.cpp
)
target_include_directories(merak-loop PUBLIC include)
target_link_libraries(merak-loop PUBLIC
    merak-core
    merak-config
    merak-llm
    merak-memory
    merak-tools
    merak-context
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
