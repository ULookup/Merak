cmake_minimum_required(VERSION 3.22)
project(merak-agent VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 使用 g++-13（支持 C++23 std::expected）
set(CMAKE_C_COMPILER gcc-13)
set(CMAKE_CXX_COMPILER g++-13)

# Conan 依赖查找
find_package(Boost REQUIRED COMPONENTS system)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(libpqxx REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(GTest REQUIRED)
find_package(ftxui REQUIRED)

# 公共编译选项
add_compile_options(-Wall -Wextra -Wpedantic)

# 子模块
add_subdirectory(libs/core)
add_subdirectory(libs/config)
add_subdirectory(libs/llm)
add_subdirectory(libs/memory)
add_subdirectory(libs/mcp)
add_subdirectory(libs/tools)
add_subdirectory(libs/context)
add_subdirectory(libs/loop)
add_subdirectory(cli)
add_subdirectory(tests)
