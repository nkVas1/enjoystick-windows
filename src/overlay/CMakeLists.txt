cmake_minimum_required(VERSION 3.20)

add_library(enjoystick_overlay STATIC
    src/OverlayWindow.cpp
    src/RadialMenu.cpp
    src/Toast.cpp
    src/HudWidget.cpp
)

target_compile_definitions(enjoystick_overlay PUBLIC
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_include_directories(enjoystick_overlay
    PUBLIC  include
    PRIVATE src
)

target_link_libraries(enjoystick_overlay PUBLIC enjoystick_core)
target_compile_features(enjoystick_overlay PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(enjoystick_overlay PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )
endif()
