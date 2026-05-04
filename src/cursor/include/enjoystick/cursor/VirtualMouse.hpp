#pragma once

#include <enjoystick/shared/Types.hpp>

#include <cstdint>

namespace enjoystick::cursor {

///
/// Configurable parameters for the virtual-mouse acceleration model.
///
/// Field names are intentionally verbose to be self-documenting at call-sites
/// such as VMConfigFromSettings() in Application.cpp.
///
struct MouseConfig {
    /// Maximum cursor speed at full stick deflection (pixels per millisecond).
    float maxSpeedPx       = 25.0f;

    /// Acceleration curve power:  1.0 = linear,  2.0 = squared.
    float curveExponent    = 2.0f;

    /// Time (ms) to ramp from rest to full speed.  0 = no ramp.
    float accelerationMs   = 0.0f;

    /// Dead-zone radius inside which no movement is generated ([0, 1]).
    float linearZone       = 0.15f;

    /// Scroll speed multiplier when using the right stick as a scroll wheel.
    float scrollSpeed      = 6.0f;

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
    /// methods are no-ops — the object retains its config and accumulator
    /// state so it resumes cleanly when re-enabled.
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
    float m_accumX    = 0.0f;
    float m_accumY    = 0.0f;
    float m_scrollAccum = 0.0f;

    // Trigger de-bounce
    bool m_ltWasDown = false;
    bool m_rtWasDown = false;
};

} // namespace enjoystick::cursor
