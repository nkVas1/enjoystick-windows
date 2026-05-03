// NOTE: WIN32_LEAN_AND_MEAN and NOMINMAX are injected by CMake for this module.
#include <enjoystick/cursor/VirtualMouse.hpp>

#include <Windows.h>

#include <algorithm>
#include <cmath>

namespace enjoystick::cursor {

VirtualMouse::VirtualMouse(Config config)
    : m_config(std::move(config))
{}

void VirtualMouse::SetConfig(Config config) noexcept {
    m_config = std::move(config);
    m_accumX = 0.0f;
    m_accumY = 0.0f;
    m_velX   = 0.0f;
    m_velY   = 0.0f;
}

void VirtualMouse::SetEnabled(bool enabled) noexcept {
    m_enabled = enabled;
    if (!enabled) {
        m_accumX = m_accumY = 0.0f;
        m_velX   = m_velY   = 0.0f;
    }
}

bool VirtualMouse::IsEnabled() const noexcept { return m_enabled; }

// ---------------------------------------------------------------------------
// Update  — called from the input thread at polling rate (e.g. 250 Hz)
// ---------------------------------------------------------------------------
void VirtualMouse::Update(const ControllerState& state, float deltaSeconds) {
    if (!m_enabled) return;

    const Vec2 stick = m_config.useRightStick ? state.rightStick : state.leftStick;

    // Apply acceleration curve: speed = maxSpeed * |stick|^exponent
    const float mag = std::sqrt(stick.LengthSq());
    if (mag < 1e-4f) {
        // Decelerate smoothly to zero when stick is released
        const float decay = (m_config.accelerationMs > 0.0f)
            ? std::exp(-deltaSeconds * 1000.0f / m_config.accelerationMs)
            : 0.0f;
        m_velX *= decay;
        m_velY *= decay;
    } else {
        const float speed = m_config.maxSpeedPx *
                            std::pow(mag, m_config.curveExponent);
        const float dirX  = (mag > 1e-6f) ? stick.x / mag : 0.0f;
        const float dirY  = (mag > 1e-6f) ? stick.y / mag : 0.0f;

        // Smoothly interpolate velocity toward target
        const float alpha = (m_config.accelerationMs > 0.0f)
            ? std::min(1.0f, deltaSeconds * 1000.0f / m_config.accelerationMs)
            : 1.0f;
        m_velX = m_velX + (dirX * speed - m_velX) * alpha;
        m_velY = m_velY + (dirY * speed - m_velY) * alpha;
    }

    // Sub-pixel accumulation
    m_accumX += m_velX * deltaSeconds;
    m_accumY += m_velY * deltaSeconds;

    const int dx = static_cast<int>(m_accumX);
    const int dy = static_cast<int>(m_accumY);

    if (dx != 0 || dy != 0) {
        m_accumX -= static_cast<float>(dx);
        m_accumY -= static_cast<float>(dy);
        MoveMouse(dx, dy);
    }

    // Scroll: left trigger = up, right trigger = down (or inverted)
    const float scrollDelta = (state.leftTrigger - state.rightTrigger) *
                              m_config.scrollSpeed *
                              (m_config.invertScroll ? -1.0f : 1.0f);
    if (std::abs(scrollDelta) > 0.01f) {
        SendScroll(static_cast<int>(scrollDelta * 10.0f));
    }

    // Trigger-as-click
    if (m_config.triggersAsClicks) {
        HandleTriggerClick(state.leftTrigger,  m_prevLT, MOUSEEVENTF_LEFTDOWN,  MOUSEEVENTF_LEFTUP);
        HandleTriggerClick(state.rightTrigger, m_prevRT, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
    }
    m_prevLT = state.leftTrigger;
    m_prevRT = state.rightTrigger;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void VirtualMouse::MoveMouse(int dx, int dy) {
    INPUT inp{};
    inp.type           = INPUT_MOUSE;
    inp.mi.dwFlags     = MOUSEEVENTF_MOVE;
    inp.mi.dx          = dx;
    inp.mi.dy          = dy;
    SendInput(1, &inp, sizeof(INPUT));
}

void VirtualMouse::SendScroll(int ticks) {
    INPUT inp{};
    inp.type         = INPUT_MOUSE;
    inp.mi.dwFlags   = MOUSEEVENTF_WHEEL;
    inp.mi.mouseData = static_cast<DWORD>(ticks * WHEEL_DELTA);
    SendInput(1, &inp, sizeof(INPUT));
}

void VirtualMouse::HandleTriggerClick(
    float current, float prev,
    DWORD downFlag, DWORD upFlag)
{
    constexpr float kThreshold = 0.5f;
    const bool wasDown = (prev    >= kThreshold);
    const bool isDown  = (current >= kThreshold);

    if (isDown && !wasDown) {
        INPUT inp{};
        inp.type       = INPUT_MOUSE;
        inp.mi.dwFlags = downFlag;
        SendInput(1, &inp, sizeof(INPUT));
    } else if (!isDown && wasDown) {
        INPUT inp{};
        inp.type       = INPUT_MOUSE;
        inp.mi.dwFlags = upFlag;
        SendInput(1, &inp, sizeof(INPUT));
    }
}

} // namespace enjoystick::cursor
