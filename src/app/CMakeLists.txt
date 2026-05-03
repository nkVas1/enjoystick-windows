cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# enjoystick_app  (static library — no main())
#
# Application-layer wiring: composes all subsystems, owns the Win32
# message loop and system-tray icon.
#
# The executable entry point (wWinMain) lives in src/main.cpp at the
# repository root and is compiled directly into the enjoystick.exe target
# defined in the root CMakeLists.txt.  It must NOT appear here.
# ---------------------------------------------------------------------------

add_library(enjoystick_app STATIC
    src/Application.cpp
    src/SystemTray.cpp
    src/AutoStart.cpp
    src/KeyboardMapper.cpp
    # Note: main.cpp is intentionally excluded — entry point is in
    #       the root src/main.cpp linked by the enjoystick executable.
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
