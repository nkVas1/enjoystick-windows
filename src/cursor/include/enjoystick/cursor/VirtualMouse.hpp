#pragma once

#include <enjoystick/shared/Types.hpp>

#include <cstdint>

namespace enjoystick::cursor {

// ---------------------------------------------------------------------------
// Per-monitor calibration data (computed once per monitor change)
// ---------------------------------------------------------------------------
struct MonitorProfile {
    float    widthPx    = 1920.0f;
    float    heightPx   = 1080.0f;
    float    dpiX       = 96.0f;
    float    dpiY       = 96.0f;
    float    scaleX     = 1.0f;
    float    scaleY     = 1.0f;
    float    approxPpi  = 96.0f;
    /// Final speed multiplier applied to maxSpeedPx.
    float    speedScale = 1.0f;
};

// ---------------------------------------------------------------------------
// MouseConfig
// ---------------------------------------------------------------------------
struct MouseConfig {
    /// Base max speed at full stick deflection (px/ms).
    /// Reference: 6 px/ms on 1920x1080 @ 96 dpi.
    float maxSpeedPx          = 6.0f;
    /// Acceleration curve power.  1.35 is recommended.
    float curveExponent       = 1.35f;
    /// Ramp duration (ms) from rest to full speed.
    float accelerationMs      = 80.0f;
    /// Dead-zone radius in normalised [0,1] stick space.
    float linearZone          = 0.10f;
    /// Scroll speed multiplier.
    float scrollSpeed         = 4.0f;
    /// Map LT/RT to mouse buttons 1/2.
    bool  triggersAsClicks    = false;
    /// true = right stick moves cursor, left stick scrolls.
    bool  useRightStick       = true;
    /// Wrap cursor at screen edges.
    bool  wrapEdges           = false;

    // --- Adaptive per-monitor calibration -----------------------------------
    /// Enable automatic per-monitor speed calibration.
    bool  adaptiveSpeed       = true;
    /// Time (ms) to traverse the full screen width at max stick deflection.
    float targetTraversalMs   = 900.0f;
    /// DPI blend weight: 0.0 = pure resolution, 1.0 = pure DPI.
    float dpiWeight           = 0.5f;
    /// Legacy field kept for config compatibility.
    float targetScreenFracPerSec = 0.20f;
    float adaptiveMinScale    = 0.30f;
    float adaptiveMaxScale    = 2.50f;
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
    [[nodiscard]] float          Accelerate(float magnitude) const noexcept;
    [[nodiscard]] float          EffectiveMaxSpeedPx() const noexcept;
    [[nodiscard]] MonitorProfile QueryActiveMonitorProfile() const noexcept;
    [[nodiscard]] float          ComputeAdaptiveScale(const MonitorProfile&) const noexcept;
    void RefreshMonitorProfileIfNeeded() noexcept;
    void PostMouseInput(long dx, long dy, unsigned long flags, int wheelDelta = 0) const;

    MouseConfig    m_config;
    bool           m_enabled      = true;

    float          m_accumX       = 0.0f;
    float          m_accumY       = 0.0f;
    float          m_scrollAccum  = 0.0f;

    bool           m_ltWasDown    = false;
    bool           m_rtWasDown    = false;

    MonitorProfile m_monitorProfile;
    /// Cached HMONITOR stored as opaque integer to avoid Windows.h in header.
    uintptr_t      m_cachedMonitor = 0;
};

} // namespace enjoystick::cursor
