# Minimum set to 3.20 to support the CMake bundled with VS 2019 Build Tools.
# All features we use (C++20, target_link_libraries, generator expressions) are
# available since CMake 3.14; 3.20 adds the FILE_SET feature we may use later.
cmake_minimum_required(VERSION 3.20)
project(enjoystick-windows VERSION 0.1.0 LANGUAGES CXX)

# ─── C++ standard ──────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Compiler flags ────────────────────────────────────────────────────────────
if (MSVC)
    # /WX (warnings-as-errors) intentionally omitted here: third-party headers
    # often emit W4 warnings we cannot fix. Enable per-target if desired.
    add_compile_options(
        /W4
        /permissive-        # Strict ISO conformance
        /Zc:__cplusplus     # Report correct __cplusplus value
        /utf-8              # Source and execution charset = UTF-8
        /MP                 # Parallel file compilation
        $<$<CONFIG:Release>:/O2 /GL>
        $<$<CONFIG:Debug>:/Od /RTC1 /Zi>
    )
    add_link_options(
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
        $<$<CONFIG:Debug>:/DEBUG:FULL>
    )
endif()

# ─── Optional: vcpkg toolchain ─────────────────────────────────────────────────
# If VCPKG_ROOT is set in the environment, wire it up automatically so the user
# does not have to pass -DCMAKE_TOOLCHAIN_FILE on the command line.
if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if (DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE
            "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
            CACHE STRING "vcpkg toolchain" FORCE)
        message(STATUS "[enjoystick] Using vcpkg toolchain: ${CMAKE_TOOLCHAIN_FILE}")
    else()
        message(STATUS "[enjoystick] VCPKG_ROOT not set — vcpkg integration disabled.")
        message(STATUS "             Set VCPKG_ROOT env var or pass "
                       "-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake")
    endif()
endif()

# ─── Subsystem modules ─────────────────────────────────────────────────────────
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

# ─── Executable ────────────────────────────────────────────────────────────────
add_executable(enjoystick WIN32 src/main.cpp)

target_link_libraries(enjoystick
    PRIVATE enjoystick_app
)

set_target_properties(enjoystick PROPERTIES
    OUTPUT_NAME "EnjoyStick"
)

target_compile_features(enjoystick PRIVATE cxx_std_20)

# ─── Install ───────────────────────────────────────────────────────────────────
install(TARGETS enjoystick DESTINATION .)

message(STATUS "[enjoystick] CMake ${CMAKE_VERSION} — Generator: ${CMAKE_GENERATOR}")
message(STATUS "[enjoystick] Build type: ${CMAKE_BUILD_TYPE}")
