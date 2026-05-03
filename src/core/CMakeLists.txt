cmake_minimum_required(VERSION 3.25)

add_library(enjoystick_core STATIC
    src/InputEngine.cpp
    src/InputBackend_XInput.cpp
    src/DeadzoneFilter.cpp
)

target_include_directories(enjoystick_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(enjoystick_core PUBLIC cxx_std_20)

set_target_properties(enjoystick_core PROPERTIES
    CXX_EXTENSIONS OFF
)
