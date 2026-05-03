cmake_minimum_required(VERSION 3.20)

add_library(enjoystick_input STATIC
    src/KeyboardMapper.cpp
)

target_include_directories(enjoystick_input
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(enjoystick_input
    PUBLIC enjoystick_core
)

target_compile_features(enjoystick_input PUBLIC cxx_std_20)

set_target_properties(enjoystick_input PROPERTIES
    CXX_EXTENSIONS OFF
)
