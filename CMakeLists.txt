cmake_minimum_required(VERSION 3.28)

project(
    EnjoystickWindows
    VERSION 0.1.0
    DESCRIPTION "Seamless gamepad UX/UI extension for Windows 11"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Treat warnings as errors
add_compile_options(
    /W4
    /WX
    /permissive-
    /Zc:__cplusplus
    /utf-8
    $<$<CONFIG:Release>:/O2>
    $<$<CONFIG:Release>:/GL>
)

add_link_options(
    $<$<CONFIG:Release>:/LTCG>
)

# Global definitions
add_compile_definitions(
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
)

# Options
option(ENJOYSTICK_BUILD_TESTS "Build test projects" ON)
option(ENJOYSTICK_BUILD_BENCHMARKS "Build benchmark projects" OFF)
option(ENJOYSTICK_ENABLE_TELEMETRY "Enable opt-in anonymous telemetry" OFF)

# Dependencies (via vcpkg or FetchContent)
find_package(SDL3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

# Subdirectories
add_subdirectory(src/core)
add_subdirectory(src/inputrouter)
add_subdirectory(src/appadapter)
add_subdirectory(src/mediaengine)
add_subdirectory(src/shared)

if(ENJOYSTICK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
