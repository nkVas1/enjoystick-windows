#pragma once

#include <enjoystick/shared/Types.hpp>

namespace enjoystick::core {

/// Configuration for VirtualMouse.
struct MouseConfig {
    // -----------------------------------------------------------------------
    // Pointer motion
    // -----------------------------------------------------------------------
    float cursorSpeed        = 8.0f;    ///< Base pixels/second at full stick deflection
    float curveExponent      = 1.5f;    ///< Power-law curve (1.0 = linear)
    float accelerationMs     = 120.0f;  ///< Time (ms) to ramp from 0 to full speed
    bool  useRightStick      = false;   ///< false = left stick drives pointer

    // Cross-axis suppression: attenuate the minor axis when motion is
    // strongly cardinal, making straight-line movement much easier.
    float crossAxisThreshold = 0.85f;  ///< Dominant-axis ratio above which snap activates
    float crossAxisDamp      = 0.35f;  ///< Minor-axis attenuation factor [0, 1]

    // -----------------------------------------------------------------------
    // Scrolling
    // -----------------------------------------------------------------------
    float scrollSpeed        = 12.0f;  ///< Scroll clicks/second at full deflection
    float scrollAccelStartMs = 600.0f; ///< Hold duration before scroll accel ramp begins
    float scrollSpeedMax     = 3.0f;   ///< Maximum scroll speed multiplier
    float scrollAccelRampMs  = 1200.0f;///< Duration of full speed ramp
    bool  triggersAsClicks   = false;  ///< LT = right-click, RT = left-click

    // -----------------------------------------------------------------------
    // Adaptive speed
    // -----------------------------------------------------------------------
    bool  adaptiveSpeed      = false;  ///< Adjust speed to match screen DPI
    float targetTraversalMs  = 800.0f; ///< Target time to cross the screen (ms)
    float dpiWeight          = 0.5f;   ///< Blend between fixed and adaptive speed

    // -----------------------------------------------------------------------
    // Multi-monitor
    // -----------------------------------------------------------------------
    bool  wrapEdges          = false;  ///< Wrap cursor at virtual desktop edges

    // -----------------------------------------------------------------------
    // Game-mode left-stick lock
    //
    // When leftStickGameModeOnly is true (set by OverlayController when
    // GameModeDetector reports a game is in the foreground), the left stick
    // is used exclusively for pointer movement.  Scroll and any other
    // overlay-side navigation driven by the left stick are suppressed.
    //
    // This prevents the stick from accidentally scrolling the overlay or
    // triggering navigation shortcuts while the user is playing a game.
    // -----------------------------------------------------------------------
    bool  leftStickGameModeOnly = false;
};

///
/// VirtualMouse — translates controller stick input to cursor movement
/// and scroll wheel events via Windows SendInput.
///
class VirtualMouse {
public:
    explicit VirtualMouse(MouseConfig config = {});
    ~VirtualMouse() = default;

    void  SetConfig(MouseConfig config) noexcept;
    [[nodiscard]] const MouseConfig& GetConfig() const noexcept { return m_config; }

    /// Drive pointer and scroll from the current controller state.
    /// deltaSeconds is the frame time in seconds.
    void Update(const ControllerState& state, float deltaSeconds) noexcept;

    /// Immediately stop all motion (e.g., when overlay opens).
    void Reset() noexcept;

private:
    MouseConfig m_config;

    // Motion state
    float m_accelTimer   = 0.0f;
    float m_velX         = 0.0f;
    float m_velY         = 0.0f;

    // Scroll state
    float m_scrollAccum  = 0.0f;
    float m_scrollHoldMs = 0.0f;

    float Accelerate(float magnitude) const noexcept;
    void  ApplyCrossAxisSuppression(float& nx, float& ny) const noexcept;
};

} // namespace enjoystick::core
