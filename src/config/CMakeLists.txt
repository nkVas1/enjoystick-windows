cmake_minimum_required(VERSION 3.14)

# ---------------------------------------------------------------------------
# enjoystick_config
#
# JSON configuration store with ReadDirectoryChangesW-based hot reload.
# Persists to %APPDATA%\Enjoystick\config.json
# ---------------------------------------------------------------------------

add_library(enjoystick_config STATIC
    src/ConfigStore.cpp
    src/ConfigSerializer.cpp
)

target_compile_definitions(enjoystick_config PUBLIC
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_include_directories(enjoystick_config
    PUBLIC  include
    PRIVATE src
)

# shell32  — SHGetKnownFolderPath (config path resolution)
# ole32    — CoTaskMemFree
# shlwapi  — path helpers (PathFileExistsW etc., used by watcher)
target_link_libraries(enjoystick_config
    PUBLIC  enjoystick_core enjoystick_cursor enjoystick_input
    PRIVATE shell32 ole32 shlwapi
)

target_compile_features(enjoystick_config PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(enjoystick_config PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )
endif()
