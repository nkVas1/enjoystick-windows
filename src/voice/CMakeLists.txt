cmake_minimum_required(VERSION 3.14)

# ---------------------------------------------------------------------------
# enjoystick_voice
#
# Windows SAPI 5 continuous dictation wrapper.
# Supports ru-RU (primary) and en-US (secondary).
# Requires: sapi.lib (shipped with the Windows SDK)
# ---------------------------------------------------------------------------

add_library(enjoystick_voice STATIC
    src/VoiceInput.cpp
    src/VoiceInputEngine.cpp
)

target_compile_definitions(enjoystick_voice PUBLIC
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_include_directories(enjoystick_voice
    PUBLIC  include
    PRIVATE src
)

target_link_libraries(enjoystick_voice
    PUBLIC  enjoystick_core
    PRIVATE sapi ole32 oleaut32
)

target_compile_features(enjoystick_voice PUBLIC cxx_std_20)

if (MSVC)
    target_compile_options(enjoystick_voice PRIVATE
        /W4
        /permissive-
        /Zc:__cplusplus
        /utf-8
    )
endif()
