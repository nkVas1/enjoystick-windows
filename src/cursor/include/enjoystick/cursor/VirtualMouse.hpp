#pragma once

#include <enjoystick/shared/Types.hpp>

#include <cstdint>

namespace enjoystick::cursor {

///
/// Configurable parameters for the virtual-mouse acceleration model.
///
/// Defaults are now intentionally conservative. Final movement speed is
/// auto-normalised at runtime against the active monitor's physical density,
/// DPI scale and resolution, so an HD display no longer feels "too fast"
/// while QHD/4K screens remain comfortably responsive.
///
struct MouseConfig {
    /// Reference speed at full stick deflection (pixels per millisecond)
    /// before monitor-aware normalisation is applied.
    float maxSpeedPx       = 4.2f;

    /// Acceleration curve power. 1.0 = linear. 1.30 keeps micro-movement
    /// extremely controllable while still allowing brisk travel at full tilt.
    float curveExponent    = 1.30f;

    /// Ramp duration (ms) from rest to full speed.
    float accelerationMs   = 95.0f;

    /// Dead-zone radius inside which no movement is generated ([0, 1]).
    float linearZone       = 0.09f;

    /// Scroll speed multiplier when right/left spare stick drives wheel.
    float scrollSpeed      = 4.0f;

    /// Whether left/right triggers fire mouse button 1 / 2.
    bool  triggersAsClicks = false;

    /// Use right stick for cursor movement (true) or left stick (false).
    bool  useRightStick    = true;

    /// Wrap cursor position at screen edges instead of clamping.
    bool  wrapEdges        = false;

    /// Enable monitor-aware runtime normalisation of cursor speed.
    bool  adaptiveSpeed    = true;

    /// Desired fraction of the active monitor width traversed per second when
    /// the stick is fully deflected. 0.26 feels deliberate and premium.
    float targetScreenFracPerSec = 0.26f;

    /// Lower clamp for monitor speed multiplier.
    float adaptiveMinScale = 0.38f;

    /// Upper clamp for monitor speed multiplier.
    float adaptiveMaxScale = 0.92f;
};

class VirtualMouse {
public:
    explicit VirtualMouse(MouseConfig config = {});

    void SetConfig(MouseConfig config);
    const MouseConfig& GetConfig() const noexcept;

    void SetEnabled(bool enabled) noexcept;
    [[nodiscard]] bool IsEnabled() const noexcept;

    void Update(const ControllerState& state, float deltaMs);
    void Update(float dx, float dy, float deltaMs);
    void ScrollStick(float dy, float deltaMs);

    void LeftClick();
    void RightClick();
    void MiddleClick();
    void LeftDown();
    void LeftUp();

private:
    struct MonitorProfile {
        float widthPx      = 1920.0f;
        float heightPx     = 1080.0f;
        float dpiX         = 96.0f;
        float dpiY         = 96.0f;
        float scaleX       = 1.0f;
        float scaleY       = 1.0f;
        float approxPpi    = 96.0f;
        float speedScale   = 1.0f;
    };

    [[nodiscard]] float Accelerate(float magnitude) const noexcept;
    [[nodiscard]] float EffectiveMaxSpeedPx() const noexcept;
    [[nodiscard]] MonitorProfile QueryActiveMonitorProfile() const noexcept;
    [[nodiscard]] float ComputeAdaptiveScale(const MonitorProfile& profile) const noexcept;
    void RefreshMonitorProfileIfNeeded() noexcept;
    void PostMouseInput(long dx, long dy, unsigned long flags, int wheelDelta = 0) const;

    MouseConfig m_config;
    bool        m_enabled      = true;

    float m_accumX      = 0.0f;
    float m_accumY      = 0.0f;
    float m_scrollAccum = 0.0f;

    bool m_ltWasDown = false;
    bool m_rtWasDown = false;

    mutable HMONITOR      m_cachedMonitor  = nullptr;
    mutable MonitorProfile m_monitorProfile{};
};

} // namespace enjoystick::cursor
