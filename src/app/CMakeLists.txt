cmake_minimum_required(VERSION 3.20)

add_library(enjoystick_app STATIC
    src/Application.cpp
    src/AutoStart.cpp
    src/SystemTray.cpp
)

target_include_directories(enjoystick_app
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(enjoystick_app
    PUBLIC enjoystick_core
    PUBLIC enjoystick_cursor
    PUBLIC enjoystick_input
    PUBLIC enjoystick_overlay
    PUBLIC enjoystick_config
    PRIVATE comctl32.lib
)

target_compile_features(enjoystick_app PUBLIC cxx_std_20)

set_target_properties(enjoystick_app PROPERTIES
    CXX_EXTENSIONS OFF
)
