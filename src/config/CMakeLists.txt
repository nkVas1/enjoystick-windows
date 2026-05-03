cmake_minimum_required(VERSION 3.25)

add_library(enjoystick_config STATIC
    src/ConfigStore.cpp
)

target_include_directories(enjoystick_config
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(enjoystick_config
    PUBLIC enjoystick_core
    PUBLIC enjoystick_cursor
    PUBLIC enjoystick_input
)

target_compile_features(enjoystick_config PUBLIC cxx_std_20)

set_target_properties(enjoystick_config PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)
