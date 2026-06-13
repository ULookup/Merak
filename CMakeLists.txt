cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED CMAKE_C_COMPILER AND NOT DEFINED CMAKE_CXX_COMPILER)
    find_program(MERAK_GCC13 gcc-13)
    find_program(MERAK_GXX13 g++-13)
    if(MERAK_GCC13 AND MERAK_GXX13)
        set(CMAKE_C_COMPILER ${MERAK_GCC13})
        set(CMAKE_CXX_COMPILER ${MERAK_GXX13})
    endif()
endif()

project(merak-agent VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Conan 依赖查找
find_package(Boost REQUIRED COMPONENTS system)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(libpqxx REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(GTest REQUIRED)
find_package(ftxui REQUIRED)
find_package(httplib REQUIRED)
find_package(SQLite3 REQUIRED)

# 公共编译选项
add_compile_options(-Wall -Wextra -Wpedantic)

enable_testing()

# 子模块
add_subdirectory(libs/core)
add_subdirectory(libs/config)
add_subdirectory(libs/llm)
add_subdirectory(libs/memory)
add_subdirectory(libs/mcp)
add_subdirectory(libs/tools)
add_subdirectory(libs/knowledge_graph)
add_subdirectory(libs/context)
add_subdirectory(libs/prompts)
add_subdirectory(libs/context_dsl)
add_subdirectory(libs/loop)
add_subdirectory(libs/storage)
add_subdirectory(libs/worldbuilding)
add_subdirectory(libs/portable_pg)
add_subdirectory(libs/runtime)
add_subdirectory(libs/skills)
add_subdirectory(libs/http)
add_subdirectory(cli)
# add_subdirectory(tests)

# Install pipeline configuration files
install(DIRECTORY config/pipelines/
        DESTINATION share/merak/pipelines
        FILES_MATCHING PATTERN "*.json")


