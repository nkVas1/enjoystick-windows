cmake_minimum_required(VERSION 3.14)

# ---------------------------------------------------------------------------
# enjoystick_overlay
#
# Direct2D / DirectWrite layered-window overlay.
# Renders: active indicator, toast notifications, radial menu,
#          settings menu, virtual keyboard, controls reference.
# ---------------------------------------------------------------------------

add_library(enjoystick_overlay STATIC
    src/ControlsOverlay.cpp
    src/OverlayWindow.cpp
    src/RadialMenu.cpp
    src/SettingsMenu.cpp
    src/VirtualKeyboard.cpp
)

target_compile_definitions(enjoystick_overlay PUBLIC
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_include_directories(enjoystick_overlay
    PUBLIC  include
    PRIVATE src
)

target_link_libraries(enjoystick_overlay
    PUBLIC  enjoystick_core enjoystick_config
    PRIVATE d2d1 dwrite
)

target_compile_features(enjoystick_overlay PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(enjoystick_overlay PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )
endif()
