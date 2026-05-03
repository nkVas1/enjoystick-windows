cmake_minimum_required(VERSION 3.25)

add_library(enjoystick_cursor STATIC
    src/VirtualMouse.cpp
)

target_include_directories(enjoystick_cursor
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(enjoystick_cursor
    PUBLIC enjoystick_core
)

target_compile_features(enjoystick_cursor PUBLIC cxx_std_20)

set_target_properties(enjoystick_cursor PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)
