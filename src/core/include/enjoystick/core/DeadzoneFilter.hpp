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
        Radial,   ///< Circular deadzone (default — recommended for analogue sticks)
        Axial,    ///< Independent per-axis deadzones
        Hybrid,   ///< Radial inner + axial outer
        Square,   ///< Square deadzone — maximum extent along each axis is preserved;
                  ///  maps the square stick range to the full [-1,1] output square.
    };
    Shape shape = Shape::Radial;

    // -----------------------------------------------------------------------
    // Cardinal snap (cross-axis suppression)
    //
    // When the dominant axis component is > cardinalSnapThreshold × magnitude,
    // the minor axis is attenuated by cardinalSnapDamp.  This makes it much
    // easier to scroll or move a cursor in a perfectly straight line.
    //
    // Set cardinalSnapThreshold to 1.0f to disable entirely.
    // -----------------------------------------------------------------------
    float cardinalSnapThreshold = 0.90f;   ///< Dominant-axis ratio to trigger snap
    float cardinalSnapDamp      = 0.30f;   ///< Minor-axis attenuation factor [0,1]
    bool  cardinalSnapEnabled   = false;   ///< Off by default (opt-in per use-case)
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
        Vec2 result;
        switch (m_config.shape) {
            case DeadzoneConfig::Shape::Axial:   result = ApplyAxial(raw);   break;
            case DeadzoneConfig::Shape::Hybrid:  result = ApplyHybrid(raw);  break;
            case DeadzoneConfig::Shape::Square:  result = ApplySquare(raw);  break;
            default:                             result = ApplyRadial(raw);  break;
        }
        if (m_config.cardinalSnapEnabled) {
            ApplyCardinalSnap(result);
        }
        return result;
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

    // -----------------------------------------------------------------------
    // Square deadzone
    //
    // The raw stick lies in a unit square [-1,1]^2.  Hardware sticks produce
    // less than 1.0 at corners, so we scale each axis independently:
    //   1. Apply per-axis inner deadzone (same as Axial).
    //   2. Remap each axis so that the outer boundary (innerRadius..outerRadius)
    //      maps linearly to [0, 1], giving a full square output range.
    //
    // This is the preferred mode for UI navigation and grid-based tasks.
    // -----------------------------------------------------------------------
    [[nodiscard]] Vec2 ApplySquare(Vec2 v) const noexcept {
        const float inner = m_config.innerRadius;
        const float outer = m_config.outerRadius;
        const float range = std::max(outer - inner, 1e-6f);

        auto remap = [&](float c) -> float {
            const float a = std::abs(c);
            if (a <= inner) return 0.0f;
            if (a >= outer) return c < 0 ? -1.0f : 1.0f;
            return ((a - inner) / range) * (c < 0 ? -1.0f : 1.0f);
        };

        return { remap(v.x), remap(v.y) };
    }

    // -----------------------------------------------------------------------
    // Cardinal snap: attenuate the minor axis when motion is strongly axial.
    // Called on the already-processed output vector.
    // -----------------------------------------------------------------------
    void ApplyCardinalSnap(Vec2& v) const noexcept {
        const float mag = std::sqrt(v.x * v.x + v.y * v.y);
        if (mag < 1e-6f) return;
        const float ax = std::abs(v.x) / mag;
        const float ay = std::abs(v.y) / mag;
        const float thr  = m_config.cardinalSnapThreshold;
        const float damp = m_config.cardinalSnapDamp;
        if (ax > thr)       v.y *= damp;
        else if (ay > thr)  v.x *= damp;
    }

    DeadzoneConfig m_config;
};

} // namespace enjoystick::core
