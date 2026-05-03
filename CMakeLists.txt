cmake_minimum_required(VERSION 3.25)
project(EnjoyStickWindows VERSION 0.2.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# Force MSVC Unicode
add_compile_definitions(UNICODE _UNICODE)

if (MSVC)
    add_compile_options(/W4 /WX /permissive- /Zc:__cplusplus /utf-8)
    # Disable warnings for external headers
    add_compile_options(/external:anglebrackets /external:W0)
endif()

# ---------------------------------------------------------------------------
# Output directories
# ---------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------------
# Submodules
# ---------------------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/cursor)
add_subdirectory(src/overlay)
add_subdirectory(src/config)
add_subdirectory(src/app)

# ---------------------------------------------------------------------------
# Install rules (optional, for packaging)
# ---------------------------------------------------------------------------
install(TARGETS enjoystick
    RUNTIME DESTINATION bin
)
install(FILES
    $<TARGET_RUNTIME_DLLS:enjoystick>
    DESTINATION bin
)
