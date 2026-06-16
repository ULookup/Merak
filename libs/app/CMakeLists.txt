add_library(merak-app STATIC
    src/application.cpp
)
target_include_directories(merak-app PUBLIC include)
target_link_libraries(merak-app PUBLIC
    merak-core merak-config merak-llm merak-memory merak-mcp
    merak-tools merak-context merak-loop
    merak-worldbuilding merak-runtime merak-skills merak-http
    merak-storage merak-prompts merak-portable-pg merak-portable-neo4j
)
target_compile_features(merak-app PUBLIC cxx_std_23)
