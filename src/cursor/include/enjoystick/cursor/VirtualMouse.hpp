#pragma once

#include <enjoystick/shared/Types.hpp>
#include <enjoystick/core/InputEngine.hpp>
#include <cstdint>
#include <memory>

namespace enjoystick::cursor {

///
/// VirtualMouse — translates right-stick / left-stick analog input into
/// smooth, acceleration-aware Win32 cursor movement using SendInput().
///
/// Design principles:
///   - Velocity curve:  sqrt-shaped for precision at low deflection, linear at high
///   - Frame-rate independent: integrates delta-time from QueryPerformanceCounter
///   - Click emulation:  RT = left-click, LT = right-click (configurable)
///   - Scroll emulation: D-pad Up/Down or second stick with modifier held
///
class VirtualMouse {
public:
    struct Config {
        /// Maximum cursor speed in pixels/second at full stick deflection.
        float maxSpeedPx      = 2000.0f;

        /// Velocity curve exponent: 1.0 = linear, 0.5 = sqrt (recommended).
        float curveExponent   = 0.55f;

        /// Acceleration ramp-up time in seconds (0 = instant).
        float accelerationMs  = 80.0f;

        /// Which stick drives the cursor (true = right, false = left).
        bool  useRightStick   = true;

        /// Enable trigger-to-click mapping.
        bool  triggersAsClicks = true;

        /// Scroll lines per second at full deflection.
        float scrollSpeed     = 8.0f;

        /// Scroll is inverted (natural scrolling).
        bool  invertScroll    = false;
    };

    explicit VirtualMouse(Config config = {});
    ~VirtualMouse();

    /// Feed a new controller state; call on every polling tick.
    void Update(const ControllerState& state, float deltaSeconds);

    void SetConfig(Config config) noexcept;
    [[nodiscard]] const Config& GetConfig() const noexcept;

    /// Temporarily suspend cursor movement (e.g., when overlay is open).
    void SetEnabled(bool enabled) noexcept;
    [[nodiscard]] bool IsEnabled() const noexcept;

private:
    void MoveCursor(Vec2 velocity, float deltaSeconds);
    void HandleClicks(const ControllerState& state);
    void HandleScroll(const ControllerState& state, float deltaSeconds);

    [[nodiscard]] Vec2 ApplyCurve(Vec2 raw) const noexcept;

    Config   m_config;
    bool     m_enabled        = true;
    Vec2     m_currentVelocity = {};
    Vec2     m_subPixelRemainder = {};   ///< Sub-pixel accumulator for smooth movement
    float    m_scrollAccumulator = 0.0f;

    bool     m_leftButtonDown  = false;
    bool     m_rightButtonDown = false;
};

} // namespace enjoystick::cursor
