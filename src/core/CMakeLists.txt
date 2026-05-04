cmake_minimum_required(VERSION 3.14)

# ---------------------------------------------------------------------------
# enjoystick_core
#
# Low-level input subsystem:
#   InputEngine         — abstract polling interface + factory
#   InputBackend_XInput — XInput 1.4 implementation (250 Hz poll thread)
#   DeadzoneFilter      — radial / axial / hybrid deadzone math
# ---------------------------------------------------------------------------

add_library(enjoystick_core STATIC
    src/DeadzoneFilter.cpp
    src/InputEngine.cpp
    src/InputBackend_XInput.cpp
)

target_compile_definitions(enjoystick_core PUBLIC
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_include_directories(enjoystick_core
    PUBLIC  include
    PRIVATE src
)

target_link_libraries(enjoystick_core
    PRIVATE xinput
)

target_compile_features(enjoystick_core PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(enjoystick_core PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )
endif()
