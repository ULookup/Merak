add_library(merak-skills STATIC
    src/skill_loader.cpp
    src/skill_registry.cpp
    src/skill_executor.cpp
)
target_include_directories(merak-skills PUBLIC include)
target_link_libraries(merak-skills PUBLIC
    merak-core
    merak-tools
)
