#pragma once

#include <enjoystick/shared/Types.hpp>

#include <cstdint>

namespace enjoystick::cursor {

///
/// Configurable parameters for the virtual-mouse acceleration model.
///
/// Default values are tuned for comfortable desktop navigation with a
/// standard analogue stick on a 1080p display.
///
struct MouseConfig {
    /// Maximum cursor speed at full stick deflection (pixels per millisecond).
    /// 6 px/ms @ 250 Hz polling = 24 px per tick max — precise on 1080p.
    float maxSpeedPx       = 6.0f;

    /// Acceleration curve power.  1.0 = linear.  1.4 gives a very gentle
    /// roll-off that keeps the cursor precise at low deflection.
    float curveExponent    = 1.4f;

    /// Ramp duration (ms) from rest to full speed.
    /// 80 ms prevents the jarring lurch felt when you first push the stick.
    float accelerationMs   = 80.0f;

    /// Dead-zone radius inside which no movement is generated ([0, 1]).
    float linearZone       = 0.10f;

    /// Scroll speed multiplier when right stick is used as a scroll wheel.
    float scrollSpeed      = 4.0f;

    /// Whether left/right triggers fire mouse button 1 / 2.
    bool  triggersAsClicks = false;

    /// Use right stick for cursor movement (true) or left stick (false).
    bool  useRightStick    = true;

    /// Wrap cursor position at screen edges instead of clamping.
    bool  wrapEdges        = false;
};

///
/// VirtualMouse — drives the system cursor via SendInput.
///
/// Threading: NOT thread-safe. Call Update / Click from a single thread
/// (typically the input event handler).
///
class VirtualMouse {
public:
    explicit VirtualMouse(MouseConfig config = {});

    void SetConfig(MouseConfig config);
    const MouseConfig& GetConfig() const noexcept;

    /// Enable or disable all output.  When disabled, Update() and click
    /// methods are no-ops.  Config and accumulator state are preserved.
    void SetEnabled(bool enabled) noexcept;
    [[nodiscard]] bool IsEnabled() const noexcept;

    // ------------------------------------------------------------------
    // High-level overload — called from OnControllerState()
    // ------------------------------------------------------------------

    /// Convenience: extract the correct stick axes from a ControllerState
    /// (respects useRightStick), apply trigger-as-click if configured,
    /// then call the low-level Update(dx, dy, deltaMs) and ScrollStick().
    void Update(const ControllerState& state, float deltaMs);

    // ------------------------------------------------------------------
    // Low-level overloads
    // ------------------------------------------------------------------

    /// Move cursor by (dx, dy) in normalised [-1,1] space, scaled by deltaMs.
    void Update(float dx, float dy, float deltaMs);

    /// Scroll wheel using right-stick Y (positive = scroll down).
    void ScrollStick(float dy, float deltaMs);

    /// Mouse button clicks.
    void LeftClick();
    void RightClick();
    void MiddleClick();

    /// Hold / release for drag operations.
    void LeftDown();
    void LeftUp();

private:
    [[nodiscard]] float Accelerate(float magnitude) const noexcept;
    void PostMouseInput(long dx, long dy, unsigned long flags, int wheelDelta = 0) const;

    MouseConfig m_config;
    bool        m_enabled      = true;

    // Sub-pixel accumulator
    float m_accumX      = 0.0f;
    float m_accumY      = 0.0f;
    float m_scrollAccum = 0.0f;

    // Trigger de-bounce
    bool m_ltWasDown = false;
    bool m_rtWasDown = false;
};

} // namespace enjoystick::cursor
