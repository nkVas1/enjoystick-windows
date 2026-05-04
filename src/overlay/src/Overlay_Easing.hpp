#pragma once

// =============================================================================
// EnjoyStick Design System  —  Overlay_Easing.hpp
//
// Named easing functions, all mapping [0..1] -> [0..1].
// =============================================================================

#include <cmath>

#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif

namespace enjoystick::overlay {

inline float EaseOutCubic(float t) noexcept {
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

inline float EaseInCubic(float t) noexcept {
    return t * t * t;
}

inline float EaseInOutQuad(float t) noexcept {
    return (t < 0.5f) ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
}

inline float EaseOutBounce(float t) noexcept {
    constexpr float n1 = 7.5625f;
    constexpr float d1 = 2.75f;
    if (t < 1.0f / d1)           return n1 * t * t;
    if (t < 2.0f / d1)           return n1 * (t -= 1.5f   / d1) * t + 0.75f;
    if (t < 2.5f / d1)           return n1 * (t -= 2.25f  / d1) * t + 0.9375f;
    return                              n1 * (t -= 2.625f / d1) * t + 0.984375f;
}

// EaseOutBack: overshoots slightly then settles
inline float EaseOutBack(float t) noexcept {
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    const float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

} // namespace enjoystick::overlay
