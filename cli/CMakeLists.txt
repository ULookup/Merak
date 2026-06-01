add_executable(merak-cli
    src/main.cpp
    src/theme/theme.cpp
    src/output/cli_output.cpp
    src/commands/command_registry.cpp
)
set_target_properties(merak-cli PROPERTIES OUTPUT_NAME merak)
target_link_libraries(merak-cli PRIVATE merak-loop merak-config ftxui::component)
target_include_directories(merak-cli PRIVATE src)

install(TARGETS merak-cli RUNTIME DESTINATION bin)
