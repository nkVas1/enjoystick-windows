#pragma once
#include <enjoystick/shared/Types.hpp>

namespace enjoystick::cursor {

using namespace enjoystick;

class VirtualMouse final {
public:
    struct Config {
        float maxSpeedPx      = 1800.0f; ///< px/s at full deflection
        float curveExponent   = 1.8f;    ///< <1 = linear-ish; >1 = exponential
        float accelerationMs  = 120.0f;  ///< velocity ramp-up time (0 = instant)
        bool  triggersAsClicks = true;   ///< RT=LMB, LT=RMB; false = trigger scroll
        float scrollSpeed     = 8.0f;    ///< scroll ticks/s at full stick deflection
        bool  invertScroll    = false;
        bool  useRightStick   = true;    ///< false = use left stick for cursor
    };

    explicit VirtualMouse(Config config = {});
    ~VirtualMouse();

    VirtualMouse(const VirtualMouse&)            = delete;
    VirtualMouse& operator=(const VirtualMouse&) = delete;

    void SetConfig(Config config) noexcept;
    const Config& GetConfig() const noexcept;

    void SetEnabled(bool enabled) noexcept;
    bool IsEnabled() const noexcept;

    /// Must be called at the polling rate (e.g. 250 Hz).
    /// @param state        Latest controller snapshot
    /// @param deltaSeconds Time since last call in seconds
    void Update(const ControllerState& state, float deltaSeconds);

private:
    Vec2  ApplyCurve(Vec2 raw) const noexcept;
    void  MoveCursor(Vec2 velocity, float deltaSeconds);
    void  HandleClicks(const ControllerState& state);
    void  HandleScrollTrigger(const ControllerState& state, float deltaSeconds);
    void  HandleScrollStick(const ControllerState& state, float deltaSeconds);

    Config m_config;
    Vec2   m_currentVelocity   {};
    Vec2   m_subPixelRemainder {};
    float  m_scrollV           = 0.0f; ///< vertical scroll accumulator
    float  m_scrollH           = 0.0f; ///< horizontal scroll accumulator
    bool   m_enabled           = true;
    bool   m_leftButtonDown    = false;
    bool   m_rightButtonDown   = false;
};

} // namespace enjoystick::cursor
