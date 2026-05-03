cmake_minimum_required(VERSION 3.20)

add_library(enjoystick_core STATIC
    src/DeadzoneFilter.cpp
    src/InputEngine.cpp
    src/InputBackend_XInput.cpp
)

# Ensure NOMINMAX is defined project-wide for this target
target_compile_definitions(enjoystick_core PUBLIC
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_include_directories(enjoystick_core
    PUBLIC  include
    PRIVATE src
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
