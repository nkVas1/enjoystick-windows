cmake_minimum_required(VERSION 3.25)
project(enjoystick-windows VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Compiler flags ────────────────────────────────────────────────────────────
if (MSVC)
    add_compile_options(
        /W4 /WX         # Treat warnings as errors
        /permissive-    # Strict conformance mode
        /Zc:__cplusplus # Correct __cplusplus macro
        /utf-8          # Source and output encoding
        /MP             # Parallel compilation
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/GL>   # Whole program optimisation
        $<$<CONFIG:Debug>:/Od>
        $<$<CONFIG:Debug>:/RTC1>
    )
    add_link_options(
        $<$<CONFIG:Release>:/LTCG>
    )
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
    WIN32_EXECUTABLE TRUE
)

# ─── Install ───────────────────────────────────────────────────────────────────
install(TARGETS enjoystick DESTINATION .)
