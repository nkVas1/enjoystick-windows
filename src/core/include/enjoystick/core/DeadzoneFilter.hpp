#pragma once

#include <enjoystick/shared/Types.hpp>
#include <cmath>

namespace enjoystick::core {

///
/// DeadzoneFilter — implements multiple deadzone strategies for analog sticks.
///
/// The "axial" deadzone (Xbox style) introduces cardinal bias.
/// The "radial" deadzone (PlayStation style) preserves direction perfectly.
/// The "scaled radial" variant remaps the live zone to 0..1 for smooth acceleration.
///
struct DeadzoneConfig {
    enum class Mode : uint8_t {
        None,           ///< Pass-through, no filtering
        Axial,          ///< Per-axis independent threshold
        Radial,         ///< Circular threshold on magnitude
        ScaledRadial,   ///< Radial + rescale to [0,1]
    };

    Mode   mode        = Mode::ScaledRadial;
    float  innerRadius = 0.08f;  ///< Ignore input below this magnitude
    float  outerRadius = 0.98f;  ///< Clamp to 1.0 above this magnitude
};

class DeadzoneFilter {
public:
    explicit DeadzoneFilter(DeadzoneConfig config = {}) noexcept
        : m_config(config) {}

    void SetConfig(DeadzoneConfig config) noexcept { m_config = config; }
    [[nodiscard]] const DeadzoneConfig& GetConfig() const noexcept { return m_config; }

    /// Apply deadzone filtering to a raw axis vector.
    /// Input and output are in [-1, 1] range.
    [[nodiscard]] Vec2 Apply(Vec2 raw) const noexcept {
        switch (m_config.mode) {
            case DeadzoneConfig::Mode::None:
                return raw;
            case DeadzoneConfig::Mode::Axial:
                return ApplyAxial(raw);
            case DeadzoneConfig::Mode::Radial:
                return ApplyRadial(raw, /*rescale=*/false);
            case DeadzoneConfig::Mode::ScaledRadial:
                return ApplyRadial(raw, /*rescale=*/true);
        }
        return raw;
    }

private:
    [[nodiscard]] Vec2 ApplyAxial(Vec2 v) const noexcept {
        const float t = m_config.innerRadius;
        v.x = (std::abs(v.x) < t) ? 0.0f : v.x;
        v.y = (std::abs(v.y) < t) ? 0.0f : v.y;
        return v;
    }

    [[nodiscard]] Vec2 ApplyRadial(Vec2 v, bool rescale) const noexcept {
        const float mag = std::sqrt(v.x * v.x + v.y * v.y);
        if (mag < m_config.innerRadius) return {0.0f, 0.0f};
        if (mag > m_config.outerRadius) {
            // Normalize to direction and clamp to 1.0
            return {v.x / mag, v.y / mag};
        }
        if (rescale) {
            const float liveZone = m_config.outerRadius - m_config.innerRadius;
            const float scaledMag = (mag - m_config.innerRadius) / liveZone;
            return {(v.x / mag) * scaledMag, (v.y / mag) * scaledMag};
        }
        return v;
    }

    DeadzoneConfig m_config;
};

} // namespace enjoystick::core
