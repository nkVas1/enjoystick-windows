cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# enjoystick_overlay
#
# Provides:
#   OverlayWindow  — WS_EX_LAYERED Win32 window rendered with Direct2D.
#                    Owns a dedicated render thread. Toast notifications and
#                    the radial-menu widget are drawn here.
#   RadialMenu     — Controller-driven circular action menu with animation.
#
# Toast and HUD rendering are fully embedded inside OverlayWindow.cpp
# (the DrawToasts / DrawActiveIndicator methods). No separate .cpp is
# needed until Phase 2 when each becomes a standalone DWrite sub-renderer.
# ---------------------------------------------------------------------------

add_library(enjoystick_overlay STATIC
    src/OverlayWindow.cpp
    src/RadialMenu.cpp
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
    PUBLIC  enjoystick_core
    # Direct2D, DirectWrite, DirectComposition
    PRIVATE d2d1 dwrite dcomp
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
