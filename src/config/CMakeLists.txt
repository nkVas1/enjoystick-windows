cmake_minimum_required(VERSION 3.14)

# ---------------------------------------------------------------------------
# enjoystick_config
#
# JSON configuration store with ReadDirectoryChangesW-based hot reload.
# Persists to %APPDATA%\Enjoystick\config.json
#
# Dependencies:
#   nlohmann/json   - header-only JSON library (downloaded by root CMakeLists)
#   shell32 / ole32 - SHGetKnownFolderPath, CoTaskMemFree
#   shlwapi         - path helpers used by watcher
# ---------------------------------------------------------------------------

add_library(enjoystick_config STATIC
    src/Config.cpp
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
    # nlohmann/json single-header downloaded at configure time into build dir
    PRIVATE "${NLOHMANN_JSON_INCLUDE_DIR}"
)

# shell32  - SHGetKnownFolderPath  (FOLDERID_RoamingAppData)
# ole32    - CoTaskMemFree
# shlwapi  - PathFileExistsW and other path helpers
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
