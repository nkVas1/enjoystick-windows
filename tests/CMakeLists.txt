cmake_minimum_required(VERSION 3.25)

# Fetch GoogleTest (MIT license, no external cost)
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(enjoystick_tests
    test_deadzone.cpp
    test_virtual_mouse.cpp
    test_radial_menu.cpp
)

target_link_libraries(enjoystick_tests
    PRIVATE enjoystick_core
    PRIVATE enjoystick_cursor
    PRIVATE enjoystick_overlay
    PRIVATE GTest::gtest_main
)

target_compile_features(enjoystick_tests PRIVATE cxx_std_20)

include(GoogleTest)
gtest_discover_tests(enjoystick_tests)
