cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# enjoystick_app  (static library — no main())
#
# Application-layer composition root: owns Win32 message loop, system tray,
# and wires all subsystems together.
#
# Entry point (wWinMain) lives in the root src/main.cpp and is compiled
# directly into the enjoystick.exe target. It must NOT appear here.
#
# KeyboardMapper is part of enjoystick_input — do NOT list it here.
# ---------------------------------------------------------------------------

add_library(enjoystick_app STATIC
    src/Application.cpp
    src/SystemTray.cpp
    src/AutoStart.cpp
)

target_compile_definitions(enjoystick_app PUBLIC
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_include_directories(enjoystick_app
    PUBLIC  include
    PRIVATE src
)

target_link_libraries(enjoystick_app
    PUBLIC
        enjoystick_core
        enjoystick_cursor
        enjoystick_input
        enjoystick_overlay
        enjoystick_config
    PRIVATE
        shell32
        comctl32
)

target_compile_features(enjoystick_app PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(enjoystick_app PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )
endif()
