#pragma once

// =============================================================================
// EnjoyStick Design System  -  Overlay_SpringAnim.hpp  (public copy)
//
// Critically damped spring animator for UI motion.
// This header is part of the public overlay include tree so that
// VirtualKeyboard.hpp (and any future public header that needs FloatSpring
// as a member) can include it without a fragile relative path into src/.
//
// The canonical source lives at:
//   src/overlay/src/Overlay_SpringAnim.hpp
// which now just redirects here via a shim include.
// =============================================================================

#include <cmath>

namespace enjoystick::overlay {

template<typename T>
struct SpringAnim {
    T     value     = {};
    T     velocity  = {};
    T     target    = {};
    float stiffness = 220.0f;
    float damping   =  18.0f;
    float mass      =   1.0f;

    void Step(float dt) noexcept {
        if (dt <= 0.0f) return;
        if (dt > 0.05f) dt = 0.05f;
        const T spring = (target - value) * stiffness;
        const T damp   = velocity * (-damping);
        const T accel  = (spring + damp) * (1.0f / mass);
        velocity = velocity + accel * dt;
        value    = value    + velocity * dt;
    }

    void Snap(const T& newTarget) noexcept {
        target   = newTarget;
        value    = newTarget;
        velocity = {};
    }

    void SetTarget(const T& newTarget) noexcept { target = newTarget; }

    [[nodiscard]] bool IsSettled(float threshold = 0.005f) const noexcept {
        const T     diff = value - target;
        const float d2   = static_cast<float>(diff * diff);
        const float v2   = static_cast<float>(velocity * velocity);
        return (d2 < threshold * threshold) && (v2 < threshold * threshold);
    }
};

using FloatSpring = SpringAnim<float>;

} // namespace enjoystick::overlay
