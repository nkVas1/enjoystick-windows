cmake_minimum_required(VERSION 3.20)

# ---------------------------------------------------------------------------
# enjoystick_config
#
# JSON configuration store with ReadDirectoryChangesW-based hot reload.
# Persists to %APPDATA%\Enjoystick\config.json
# ---------------------------------------------------------------------------

add_library(enjoystick_config STATIC
    src/ConfigStore.cpp
)

target_compile_definitions(enjoystick_config PUBLIC
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_include_directories(enjoystick_config
    PUBLIC  include
    PRIVATE src
)

target_link_libraries(enjoystick_config PUBLIC enjoystick_core enjoystick_cursor enjoystick_input)
target_compile_features(enjoystick_config PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(enjoystick_config PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )
endif()
