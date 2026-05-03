cmake_minimum_required(VERSION 3.25)

add_library(enjoystick_overlay STATIC
    src/RadialMenu.cpp
    src/OverlayWindow.cpp
)

target_include_directories(enjoystick_overlay
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(enjoystick_overlay
    PUBLIC enjoystick_core
    PUBLIC enjoystick_cursor
)

target_compile_features(enjoystick_overlay PUBLIC cxx_std_20)

set_target_properties(enjoystick_overlay PROPERTIES
    CXX_EXTENSIONS OFF
)
