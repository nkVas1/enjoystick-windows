// WIN32_LEAN_AND_MEAN and NOMINMAX injected by CMake.
#include <Windows.h>

#include "enjoystick/cursor/VirtualMouse.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace enjoystick::cursor {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

VirtualMouse::VirtualMouse(MouseConfig config)
    : m_config(config)
{}

void VirtualMouse::SetConfig(MouseConfig config) { m_config = config; }
const MouseConfig& VirtualMouse::GetConfig() const noexcept { return m_config; }

void VirtualMouse::SetEnabled(bool enabled) noexcept {
    m_enabled = enabled;
    if (!enabled) {
        // Reset accumulators so we don't lurch on re-enable
        m_accumX    = 0.0f;
        m_accumY    = 0.0f;
        m_scrollAccum = 0.0f;
        m_ltWasDown  = false;
        m_rtWasDown  = false;
    }
}

bool VirtualMouse::IsEnabled() const noexcept { return m_enabled; }

// ---------------------------------------------------------------------------
// High-level Update (ControllerState overload)
// ---------------------------------------------------------------------------

void VirtualMouse::Update(const ControllerState& state, float deltaMs) {
    if (!m_enabled) return;

    // Choose stick axis based on config
    const Vec2 stick = m_config.useRightStick ? state.rightStick : state.leftStick;

    // Cursor movement
    Update(stick.x, stick.y, deltaMs);

    // Scroll: when right stick is used for cursor, left-stick Y scrolls;
    //         when left stick drives the cursor, right-stick Y scrolls.
    const float scrollY = m_config.useRightStick ? state.leftStick.y : state.rightStick.y;
    ScrollStick(scrollY, deltaMs);

    // Trigger-as-click
    if (m_config.triggersAsClicks) {
        const bool ltDown = state.leftTrigger  > 0.5f;
        const bool rtDown = state.rightTrigger > 0.5f;

        if (ltDown && !m_ltWasDown)  LeftDown();
        if (!ltDown && m_ltWasDown)  LeftUp();
        if (rtDown && !m_rtWasDown)  RightClick();

        m_ltWasDown = ltDown;
        m_rtWasDown = rtDown;
    }
}

// ---------------------------------------------------------------------------
// Acceleration model
//
//  |axis| in [0, linearZone]   -> speed = |axis| * maxSpeedPx       (linear)
//  |axis| in (linearZone, 1]   -> speed = pow(|axis|, curveExponent) * maxSpeedPx
//
// ---------------------------------------------------------------------------

float VirtualMouse::Accelerate(float magnitude) const noexcept {
    if (magnitude <= 0.0f) return 0.0f;
    if (magnitude <= m_config.linearZone)
        return magnitude * m_config.maxSpeedPx;
    return std::pow(magnitude, m_config.curveExponent) * m_config.maxSpeedPx;
}

// ---------------------------------------------------------------------------
// Low-level Update
// ---------------------------------------------------------------------------

void VirtualMouse::Update(float dx, float dy, float deltaMs) {
    if (!m_enabled || deltaMs <= 0.0f) return;

    const float mag = std::sqrt(dx * dx + dy * dy);
    if (mag < 1e-6f) {
        m_accumX = m_accumY = 0.0f;
        return;
    }

    const float speed = Accelerate(std::min(mag, 1.0f));
    const float nx    = dx / mag;
    const float ny    = -dy / mag;  // screen Y is inverted

    m_accumX += nx * speed * deltaMs;
    m_accumY += ny * speed * deltaMs;

    const long ix = static_cast<long>(m_accumX);
    const long iy = static_cast<long>(m_accumY);

    m_accumX -= static_cast<float>(ix);
    m_accumY -= static_cast<float>(iy);

    if (ix == 0 && iy == 0) return;

    if (m_config.wrapEdges) {
        POINT pt;
        GetCursorPos(&pt);
        const int screenW = GetSystemMetrics(SM_CXSCREEN);
        const int screenH = GetSystemMetrics(SM_CYSCREEN);
        SetCursorPos(
            (static_cast<int>(pt.x) + ix + screenW) % screenW,
            (static_cast<int>(pt.y) + iy + screenH) % screenH);
    } else {
        PostMouseInput(ix, iy, MOUSEEVENTF_MOVE);
    }
}

// ---------------------------------------------------------------------------
// Scroll
// ---------------------------------------------------------------------------

void VirtualMouse::ScrollStick(float dy, float deltaMs) {
    if (!m_enabled) return;
    if (std::abs(dy) < 1e-6f) { m_scrollAccum = 0.0f; return; }

    m_scrollAccum += dy * m_config.scrollSpeed * deltaMs;

    const long ticks = static_cast<long>(m_scrollAccum / WHEEL_DELTA);
    if (ticks == 0) return;

    m_scrollAccum -= static_cast<float>(ticks * WHEEL_DELTA);
    PostMouseInput(0, 0, MOUSEEVENTF_WHEEL, static_cast<int>(ticks * WHEEL_DELTA));
}

// ---------------------------------------------------------------------------
// Clicks
// ---------------------------------------------------------------------------

void VirtualMouse::LeftClick()  {
    if (!m_enabled) return;
    PostMouseInput(0, 0, MOUSEEVENTF_LEFTDOWN);
    PostMouseInput(0, 0, MOUSEEVENTF_LEFTUP);
}
void VirtualMouse::RightClick() {
    if (!m_enabled) return;
    PostMouseInput(0, 0, MOUSEEVENTF_RIGHTDOWN);
    PostMouseInput(0, 0, MOUSEEVENTF_RIGHTUP);
}
void VirtualMouse::MiddleClick() {
    if (!m_enabled) return;
    PostMouseInput(0, 0, MOUSEEVENTF_MIDDLEDOWN);
    PostMouseInput(0, 0, MOUSEEVENTF_MIDDLEUP);
}
void VirtualMouse::LeftDown() {
    if (!m_enabled) return;
    PostMouseInput(0, 0, MOUSEEVENTF_LEFTDOWN);
}
void VirtualMouse::LeftUp()   {
    if (!m_enabled) return;
    PostMouseInput(0, 0, MOUSEEVENTF_LEFTUP);
}

// ---------------------------------------------------------------------------
// PostMouseInput
// ---------------------------------------------------------------------------

void VirtualMouse::PostMouseInput(
    long dx, long dy, unsigned long flags, int wheelDelta) const
{
    INPUT inp{};
    inp.type         = INPUT_MOUSE;
    inp.mi.dx        = dx;
    inp.mi.dy        = dy;
    inp.mi.mouseData = static_cast<DWORD>(wheelDelta);
    inp.mi.dwFlags   = static_cast<DWORD>(flags);
    SendInput(1, &inp, sizeof(INPUT));
}

} // namespace enjoystick::cursor
