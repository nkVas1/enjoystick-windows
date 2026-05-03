cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# enjoystick_cursor
#
# Maps controller stick / trigger input to SendInput mouse events with
# configurable acceleration curve and sub-pixel accumulation.
# ---------------------------------------------------------------------------

add_library(enjoystick_cursor STATIC
    src/VirtualMouse.cpp
)

target_compile_definitions(enjoystick_cursor PUBLIC
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_include_directories(enjoystick_cursor
    PUBLIC  include
    PRIVATE src
)

target_link_libraries(enjoystick_cursor PUBLIC enjoystick_core)
target_compile_features(enjoystick_cursor PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(enjoystick_cursor PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )
endif()
