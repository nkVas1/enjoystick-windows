cmake_minimum_required(VERSION 3.14)

# ---------------------------------------------------------------------------
# enjoystick_app  (static library)
#
# Application-layer composition root: owns Win32 message loop, system tray,
# and wires all subsystems together.
#
# Entry point (wWinMain) lives in the root src/main.cpp and is compiled
# directly into the enjoystick.exe target.
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
        shlwapi
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

# ---------------------------------------------------------------------------
# Win32 resources (.rc) — compiled into an OBJECT library and linked
# into the main executable via enjoystick_app_rc.
#
# RC files cannot go into a STATIC library directly in CMake (the .res
# object is silently dropped). The workaround is an OBJECT library that
# the top-level exe target links via $<TARGET_OBJECTS:...>.
# ---------------------------------------------------------------------------
if (WIN32)
    add_library(enjoystick_app_rc OBJECT
        res/enjoystick.rc
    )
    # Make IDI_APP define visible to the RC compiler
    target_include_directories(enjoystick_app_rc PRIVATE res)
    # Expose the object files so the exe CMakeLists can consume them
    set_property(TARGET enjoystick_app_rc PROPERTY POSITION_INDEPENDENT_CODE OFF)
endif()
