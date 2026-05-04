#pragma once

// =============================================================================
// EnjoyStick Design System  —  SpringAnim.hpp
//
// Critically damped (or over-damped) spring animator.
// Suitable for physically-believable UI motion: panel slides, key hover,
// selection bounces, scale pops.
//
// Usage:
//   FloatSpring s;
//   s.target = 1.0f;
//   s.Step(deltaSeconds);
//   float v = s.value;   // current interpolated value
//
// Tweak stiffness/damping for feel:
//   Springy bounce : stiffness=220, damping=14
//   Snappy settle  : stiffness=400, damping=28
//   Smooth slide   : stiffness=120, damping=22
// =============================================================================

#include <cmath>

namespace enjoystick::overlay {

template<typename T>
struct SpringAnim {
    T     value    = {};
    T     velocity = {};
    T     target   = {};
    float stiffness = 220.0f;
    float damping   =  18.0f;
    float mass      =   1.0f;

    void Step(float dt) noexcept {
        if (dt <= 0.0f) return;
        if (dt > 0.05f) dt = 0.05f;  // clamp to avoid instability
        const T  spring  = (target - value) * stiffness;
        const T  damp    = velocity * (-damping);
        const T  accel   = (spring + damp) * (1.0f / mass);
        velocity = velocity + accel * dt;
        value    = value + velocity * dt;
    }

    void Snap(const T& newTarget) noexcept {
        target   = newTarget;
        value    = newTarget;
        velocity = {};
    }

    void SetTarget(const T& newTarget) noexcept {
        target = newTarget;
    }

    // Returns true when value has settled within threshold of target
    [[nodiscard]] bool IsSettled(float threshold = 0.005f) const noexcept {
        // Approximate: check the scalar magnitude of (value - target)
        // Works for float; specialise for Vec2 if needed.
        const T  diff  = value - target;
        const float d2 = static_cast<float>(diff * diff);
        const T  vv    = velocity;
        const float v2 = static_cast<float>(vv * vv);
        return (d2 < threshold * threshold) && (v2 < threshold * threshold);
    }
};

// Convenience aliases
using FloatSpring = SpringAnim<float>;

} // namespace enjoystick::overlay
