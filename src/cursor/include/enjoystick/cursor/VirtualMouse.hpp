#pragma once

#include <enjoystick/core/InputTypes.hpp>

#include <cstdint>

namespace enjoystick::cursor {

///
/// Configurable parameters for the virtual-mouse acceleration model.
///
struct MouseConfig {
    float maxSpeed     = 25.0f;  ///< pixels / ms at full deflection
    float exponent     = 2.0f;   ///< acceleration curve power (1.0 = linear)
    float linearZone   = 0.15f;  ///< [0,linearZone] → no acceleration
    bool  wrapEdges    = false;  ///< wrap cursor at screen edges
    float scrollSpeed  = 6.0f;   ///< scroll lines / ms at full deflection
};

///
/// VirtualMouse — drives the system cursor via SendInput.
///
/// Threading: NOT thread-safe. Call Update/Click from a single thread
/// (typically the input event handler).
///
class VirtualMouse {
public:
    explicit VirtualMouse(MouseConfig config = {});

    void SetConfig(MouseConfig config);
    const MouseConfig& GetConfig() const noexcept;

    /// Move cursor by (dx, dy) in normalised [-1,1] space, scaled by deltaMs.
    /// Call once per input event with the stick axes.
    void Update(float dx, float dy, float deltaMs);

    /// Scroll wheel. dy in normalised [-1,1]; positive = scroll down.
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

    // Sub-pixel accumulator to avoid dithering at slow speeds.
    float m_accumX = 0.0f;
    float m_accumY = 0.0f;
    float m_scrollAccum = 0.0f;
};

} // namespace enjoystick::cursor
