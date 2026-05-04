#pragma once

#include <enjoystick/shared/Types.hpp>

#include <cstdint>

namespace enjoystick::cursor {

// ---------------------------------------------------------------------------
// Per-monitor calibration data (computed once per monitor change)
// ---------------------------------------------------------------------------
struct MonitorProfile {
    float widthPx    = 1920.0f;
    float heightPx   = 1080.0f;
    float dpiX       = 96.0f;
    float dpiY       = 96.0f;
    float scaleX     = 1.0f;
    float scaleY     = 1.0f;
    float approxPpi  = 96.0f;
    float speedScale = 1.0f; ///< final multiplier applied to maxSpeedPx
};

// ---------------------------------------------------------------------------
// MouseConfig
// ---------------------------------------------------------------------------
struct MouseConfig {
    /// Maximum cursor speed at full stick deflection (pixels / millisecond).
    /// Reference: 6 px/ms on 1920×1080 @ 96 dpi.  Adaptive scaling adjusts
    /// this for every other display automatically.
    float maxSpeedPx       = 6.0f;

    /// Acceleration curve power.  1.0 = linear; 1.35 is recommended.
    float curveExponent    = 1.35f;

    /// Ramp duration (ms): time to reach full speed from rest.
    float accelerationMs   = 80.0f;

    /// Dead-zone radius in normalised [0, 1] stick space.
    float linearZone       = 0.10f;

    /// Scroll speed multiplier when right stick drives the scroll wheel.
    float scrollSpeed      = 4.0f;

    /// Map LT/RT analogue inputs to mouse buttons 1 / 2.
    bool  triggersAsClicks = false;

    /// true → right stick moves cursor, left stick scrolls.  false = swap.
    bool  useRightStick    = true;

    /// Wrap cursor at screen edges instead of clamping.
    bool  wrapEdges        = false;

    // -----------------------------------------------------------------------
    // Adaptive per-monitor calibration
    // -----------------------------------------------------------------------

    /// Enable automatic per-monitor speed calibration.  Recommended: true.
    bool  adaptiveSpeed    = true;

    /// Target time (ms) to traverse the full screen width at full stick
    /// deflection.  900 ms feels comfortable across HD–4K displays.
    float targetTraversalMs = 900.0f;

    /// How much DPI contributes to the speed calculation vs pixel resolution.
    /// 0.0 = pure resolution scaling; 1.0 = pure DPI scaling.  0.5 is ideal.
    float dpiWeight        = 0.5f;

    /// Fraction of targetTraversalMs used as the screen-traversal target.
    /// 0.12 … 0.40 are sensible values; default 0.20 (unused in v2 formula,
    /// kept for legacy config compatibility).
    float targetScreenFracPerSec = 0.20f;

    /// Minimum / maximum adaptive speed multiplier clamps.
    float adaptiveMinScale = 0.30f;
    float adaptiveMaxScale = 2.50f;
};

// ---------------------------------------------------------------------------
// VirtualMouse
// ---------------------------------------------------------------------------
class VirtualMouse {
public:
    explicit VirtualMouse(MouseConfig config = {});

    void               SetConfig(MouseConfig config);
    const MouseConfig& GetConfig() const noexcept;
    const MonitorProfile& GetMonitorProfile() const noexcept { return m_monitorProfile; }

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
    [[nodiscard]] float Accelerate(float magnitude) const noexcept;
    [[nodiscard]] float EffectiveMaxSpeedPx() const noexcept;
    [[nodiscard]] MonitorProfile QueryActiveMonitorProfile() const noexcept;
    [[nodiscard]] float ComputeAdaptiveScale(const MonitorProfile& profile) const noexcept;
    void RefreshMonitorProfileIfNeeded() noexcept;
    void PostMouseInput(long dx, long dy, unsigned long flags, int wheelDelta = 0) const;

    MouseConfig    m_config;
    bool           m_enabled      = true;

    float          m_accumX      = 0.0f;
    float          m_accumY      = 0.0f;
    float          m_scrollAccum = 0.0f;

    bool           m_ltWasDown   = false;
    bool           m_rtWasDown   = false;

    MonitorProfile m_monitorProfile;
    HMONITOR__*    m_cachedMonitor = nullptr;
};

} // namespace enjoystick::cursor
