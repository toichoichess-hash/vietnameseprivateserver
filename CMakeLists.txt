cmake_minimum_required(VERSION 3.15)
project(RobloxLauncher LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(RobloxLauncher WIN32
    src/main.cpp
    src/resource.rc
)

target_link_libraries(RobloxLauncher PRIVATE
    user32
    gdi32
    comctl32
    dwmapi
    shell32
)

set_target_properties(RobloxLauncher PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "RobloxLauncher"
)
