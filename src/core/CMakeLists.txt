add_library(enjoystick_core STATIC
    src/InputEngine.cpp
    src/ControllerManager.cpp
    src/HapticsEngine.cpp
    src/DeadzoneFilter.cpp
    src/InputBackend_XInput.cpp
    src/InputBackend_SDL3.cpp
)

target_include_directories(enjoystick_core
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(enjoystick_core
    PUBLIC  enjoystick_shared
    PRIVATE SDL3::SDL3
            spdlog::spdlog
            xinput.lib
)

target_compile_features(enjoystick_core PUBLIC cxx_std_20)
