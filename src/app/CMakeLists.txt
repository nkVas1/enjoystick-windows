cmake_minimum_required(VERSION 3.25)

add_executable(enjoystick WIN32
    src/main.cpp
    src/KeyboardMapper.cpp
    src/AutoStart.cpp
)

target_include_directories(enjoystick
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(enjoystick
    PRIVATE enjoystick_core
    PRIVATE enjoystick_cursor
    PRIVATE enjoystick_overlay
    PRIVATE enjoystick_config
)

target_compile_features(enjoystick PRIVATE cxx_std_20)

set_target_properties(enjoystick PROPERTIES
    WIN32_EXECUTABLE    ON
    CXX_EXTENSIONS      OFF
    OUTPUT_NAME         "EnjoyStick"
)

# Embed manifest for DPI-awareness and UAC
if (MSVC)
    target_sources(enjoystick PRIVATE src/enjoystick.manifest)
endif()
