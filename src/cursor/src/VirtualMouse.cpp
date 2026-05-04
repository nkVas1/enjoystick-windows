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

// ---------------------------------------------------------------------------
// Acceleration model
//
//  |axis| in [0, linearZone]   → speed = |axis| * maxSpeed           (linear)
//  |axis| in (linearZone, 1]   → speed = pow(|axis|, exp) * maxSpeed (accelerated)
//
// The linear zone prevents micro-jitter when the stick rests near centre.
// ---------------------------------------------------------------------------

float VirtualMouse::Accelerate(float magnitude) const noexcept {
    if (magnitude <= 0.0f) return 0.0f;
    if (magnitude <= m_config.linearZone)
        return magnitude * m_config.maxSpeed;
    return std::pow(magnitude, m_config.exponent) * m_config.maxSpeed;
}

// ---------------------------------------------------------------------------
// Update — called per-frame with normalised stick axes and delta time
// ---------------------------------------------------------------------------

void VirtualMouse::Update(float dx, float dy, float deltaMs) {
    if (deltaMs <= 0.0f) return;

    // Magnitude-preserve the direction vector
    const float mag = std::sqrt(dx * dx + dy * dy);
    if (mag < 1e-6f) {
        m_accumX = m_accumY = 0.0f;
        return;
    }

    const float speed = Accelerate(std::min(mag, 1.0f));
    const float nx    = dx / mag;
    const float ny    = -dy / mag;  // Y is inverted on screen

    m_accumX += nx * speed * deltaMs;
    m_accumY += ny * speed * deltaMs;

    const long ix = static_cast<long>(m_accumX);
    const long iy = static_cast<long>(m_accumY);

    m_accumX -= static_cast<float>(ix);
    m_accumY -= static_cast<float>(iy);

    if (ix == 0 && iy == 0) return;

    if (m_config.wrapEdges) {
        // Retrieve cursor then clamp + wrap
        POINT pt;
        GetCursorPos(&pt);
        const int screenW = GetSystemMetrics(SM_CXSCREEN);
        const int screenH = GetSystemMetrics(SM_CYSCREEN);

        const int nx2 = (static_cast<int>(pt.x) + ix + screenW) % screenW;
        const int ny2 = (static_cast<int>(pt.y) + iy + screenH) % screenH;
        SetCursorPos(nx2, ny2);
    } else {
        PostMouseInput(ix, iy, MOUSEEVENTF_MOVE);
    }
}

// ---------------------------------------------------------------------------
// Scroll
// ---------------------------------------------------------------------------

void VirtualMouse::ScrollStick(float dy, float deltaMs) {
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
    PostMouseInput(0, 0, MOUSEEVENTF_LEFTDOWN);
    PostMouseInput(0, 0, MOUSEEVENTF_LEFTUP);
}
void VirtualMouse::RightClick() {
    PostMouseInput(0, 0, MOUSEEVENTF_RIGHTDOWN);
    PostMouseInput(0, 0, MOUSEEVENTF_RIGHTUP);
}
void VirtualMouse::MiddleClick() {
    PostMouseInput(0, 0, MOUSEEVENTF_MIDDLEDOWN);
    PostMouseInput(0, 0, MOUSEEVENTF_MIDDLEUP);
}
void VirtualMouse::LeftDown() { PostMouseInput(0, 0, MOUSEEVENTF_LEFTDOWN); }
void VirtualMouse::LeftUp()   { PostMouseInput(0, 0, MOUSEEVENTF_LEFTUP); }

// ---------------------------------------------------------------------------
// PostMouseInput — single SendInput call
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
