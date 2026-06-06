add_library(merak-http STATIC
    src/http_server.cpp
    src/worldbuilding_http_handler.cpp
)
target_include_directories(merak-http PUBLIC include)
target_link_libraries(merak-http PUBLIC
    merak-core
    merak-config
    merak-runtime
    merak-worldbuilding
    httplib::httplib
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(merak-http PUBLIC Shell32)
endif()
