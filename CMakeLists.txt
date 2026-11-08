cmake_minimum_required(VERSION 3.15)
project(VngPsLauncher LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(VngPsLauncher WIN32
    src/main.cpp
    src/resource.rc
)

target_link_libraries(VngPsLauncher PRIVATE
    user32
    gdi32
    comctl32
    dwmapi
    shell32
)

set_target_properties(VngPsLauncher PROPERTIES
    WIN32_EXECUTABLE TRUE
    OUTPUT_NAME "VngPsLauncher"
)
