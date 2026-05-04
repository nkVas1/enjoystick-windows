#pragma once

#include <enjoystick/shared/Types.hpp>
#include <algorithm>
#include <cmath>

namespace enjoystick::core {

/// Deadzone configuration — used by both DeadzoneFilter and Application.
struct DeadzoneConfig {
    /// Stick input magnitude below this threshold is mapped to zero.
    float innerRadius = 0.08f;

    /// Stick input magnitude above this threshold is mapped to 1.0 (full).
    float outerRadius = 0.98f;

    enum class Shape : uint8_t {
        Radial,  ///< Circular deadzone (default — recommended for analogue sticks)
        Axial,   ///< Independent per-axis deadzones
        Hybrid,  ///< Radial inner + axial outer
    };
    Shape shape = Shape::Radial;
};

///
/// DeadzoneFilter — remaps raw stick/trigger values to the [0, 1] range
/// with a configurable deadzone and clamped outer edge.
///
/// Thread-safe: stateless transform (no mutable members).
///
class DeadzoneFilter {
public:
    explicit DeadzoneFilter(DeadzoneConfig config = {}) : m_config(config) {}

    void SetConfig(DeadzoneConfig config) noexcept { m_config = config; }
    [[nodiscard]] const DeadzoneConfig& GetConfig() const noexcept { return m_config; }

    /// Apply deadzone + outer-clamp to a 2D stick axis.
    [[nodiscard]] Vec2 Apply(Vec2 raw) const noexcept {
        switch (m_config.shape) {
            case DeadzoneConfig::Shape::Axial:  return ApplyAxial(raw);
            case DeadzoneConfig::Shape::Hybrid: return ApplyHybrid(raw);
            default:                            return ApplyRadial(raw);
        }
    }

    /// Apply deadzone to a 1D trigger value.
    [[nodiscard]] float ApplyTrigger(float raw) const noexcept {
        const float inner = m_config.innerRadius;
        const float outer = m_config.outerRadius;
        if (raw <= inner) return 0.0f;
        if (raw >= outer) return 1.0f;
        return (raw - inner) / (outer - inner);
    }

private:
    [[nodiscard]] Vec2 ApplyRadial(Vec2 v) const noexcept {
        const float mag   = std::sqrt(v.x * v.x + v.y * v.y);
        const float inner = m_config.innerRadius;
        const float outer = m_config.outerRadius;
        if (mag <= inner) return {};
        if (mag >= outer) {
            const float invMag = 1.0f / mag;
            return {v.x * invMag, v.y * invMag};
        }
        const float scaled = (mag - inner) / (outer - inner);
        const float invMag = 1.0f / mag;
        return {v.x * invMag * scaled, v.y * invMag * scaled};
    }

    [[nodiscard]] Vec2 ApplyAxial(Vec2 v) const noexcept {
        return {ApplyTrigger(std::abs(v.x)) * (v.x < 0 ? -1.0f : 1.0f),
                ApplyTrigger(std::abs(v.y)) * (v.y < 0 ? -1.0f : 1.0f)};
    }

    [[nodiscard]] Vec2 ApplyHybrid(Vec2 v) const noexcept {
        // Radial inner, axial outer
        Vec2 r = ApplyRadial(v);
        return ApplyAxial(r);
    }

    DeadzoneConfig m_config;
};

} // namespace enjoystick::core
