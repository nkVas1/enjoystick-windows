cmake_minimum_required(VERSION 3.14)

# ---------------------------------------------------------------------------
# enjoystick_input
#
# Translates gamepad button chords to keyboard SendInput events.
# ---------------------------------------------------------------------------

add_library(enjoystick_input STATIC
    src/KeyboardMapper.cpp
)

target_compile_definitions(enjoystick_input PUBLIC
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_include_directories(enjoystick_input
    PUBLIC  include
    PRIVATE src
)

target_link_libraries(enjoystick_input PUBLIC enjoystick_core)
target_compile_features(enjoystick_input PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(enjoystick_input PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )
endif()
