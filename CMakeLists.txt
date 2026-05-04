cmake_minimum_required(VERSION 3.14)
project(enjoystick-windows VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# NOMINMAX + WIN32_LEAN_AND_MEAN globally for all targets in this project.
# ---------------------------------------------------------------------------
add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# ---------------------------------------------------------------------------
# vcpkg toolchain - optional, auto-detected from VCPKG_ROOT env var.
# ---------------------------------------------------------------------------
if (NOT DEFINED CMAKE_TOOLCHAIN_FILE OR CMAKE_TOOLCHAIN_FILE STREQUAL "")
    if (DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
        set(CMAKE_TOOLCHAIN_FILE
            "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE STRING "vcpkg toolchain" FORCE)
        message(STATUS "[enjoystick] vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
    else()
        message(STATUS "[enjoystick] VCPKG_ROOT not set - building without vcpkg.")
    endif()
endif()

# ---------------------------------------------------------------------------
# Compiler flags (MSVC)
# ---------------------------------------------------------------------------
if (MSVC)
    add_compile_options(
        /W4
        /permissive-
        /Zc:__cplusplus
        /utf-8
        /MP
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Debug>:/RTC1>
        $<$<CONFIG:Debug>:/Zi>
    )
    add_link_options(
        $<$<CONFIG:Release>:/LTCG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
        $<$<CONFIG:Debug>:/DEBUG:FULL>
    )
endif()

# ---------------------------------------------------------------------------
# Third-party: nlohmann/json  (header-only, fetched at configure time)
# ---------------------------------------------------------------------------
include(FetchContent)

set(NLOHMANN_JSON_INCLUDE_DIR "${CMAKE_BINARY_DIR}/third_party")

if (NOT EXISTS "${NLOHMANN_JSON_INCLUDE_DIR}/nlohmann/json.hpp")
    message(STATUS "[enjoystick] Downloading nlohmann/json v3.11.3 single-header...")
    file(DOWNLOAD
        "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp"
        "${NLOHMANN_JSON_INCLUDE_DIR}/nlohmann/json.hpp"
        SHOW_PROGRESS
        STATUS _json_dl_status
        TLS_VERIFY ON
    )
    list(GET _json_dl_status 0 _json_dl_code)
    if (NOT _json_dl_code EQUAL 0)
        message(WARNING "[enjoystick] nlohmann/json download failed (${_json_dl_status}).\n"
                        "             If you are offline, place json.hpp at:\n"
                        "             ${NLOHMANN_JSON_INCLUDE_DIR}/nlohmann/json.hpp")
    else()
        message(STATUS "[enjoystick] nlohmann/json downloaded OK.")
    endif()
else()
    message(STATUS "[enjoystick] nlohmann/json already present, skipping download.")
endif()

# ---------------------------------------------------------------------------
# Subsystem modules
# ---------------------------------------------------------------------------
set(ENJOYSTICK_MODULES
    src/core
    src/cursor
    src/input
    src/overlay
    src/config
    src/app
)

foreach(mod ${ENJOYSTICK_MODULES})
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${mod}/CMakeLists.txt")
        add_subdirectory(${mod})
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Executable
#
# enjoystick_app_rc is an OBJECT library (WIN32 only) that compiles the .rc
# file so embedded resources (icon, manifest) end up inside EnjoyStick.exe.
# STATIC libraries silently drop .res objects, so we use $<TARGET_OBJECTS:...>.
# ---------------------------------------------------------------------------
if (WIN32 AND TARGET enjoystick_app_rc)
    add_executable(enjoystick WIN32
        src/main.cpp
        $<TARGET_OBJECTS:enjoystick_app_rc>
    )
else()
    add_executable(enjoystick WIN32 src/main.cpp)
endif()

target_link_libraries(enjoystick PRIVATE enjoystick_app)
target_compile_features(enjoystick PRIVATE cxx_std_20)

set_target_properties(enjoystick PROPERTIES OUTPUT_NAME "EnjoyStick")

install(TARGETS enjoystick DESTINATION .)

message(STATUS "[enjoystick] CMake ${CMAKE_VERSION}  Generator: ${CMAKE_GENERATOR}  Config: ${CMAKE_BUILD_TYPE}")
