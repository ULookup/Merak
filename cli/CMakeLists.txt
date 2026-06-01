add_executable(merak-cli src/main.cpp src/theme/theme.cpp)
target_link_libraries(merak-cli PRIVATE merak-loop merak-config)
