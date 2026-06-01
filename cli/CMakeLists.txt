add_executable(merak-cli
    src/main.cpp
    src/theme/theme.cpp
    src/output/cli_output.cpp
    src/commands/command_registry.cpp
)
target_link_libraries(merak-cli PRIVATE merak-loop merak-config)
target_include_directories(merak-cli PRIVATE src)
