#include <enjoystick/cursor/VirtualMouse.hpp>

// NOMINMAX must come before Windows.h to prevent it defining min/max macros
// that conflict with std::min / std::max on MSVC 2019.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cmath>
#include <algorithm>

namespace enjoystick::cursor {

VirtualMouse::VirtualMouse(Config config)
    : m_config(std::move(config)) {}

VirtualMouse::~VirtualMouse() {
    INPUT inputs[2] = {};
    int   count     = 0;
    if (m_leftButtonDown) {
        inputs[count].type       = INPUT_MOUSE;
        inputs[count].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        ++count;
    }
    if (m_rightButtonDown) {
        inputs[count].type       = INPUT_MOUSE;
        inputs[count].mi.dwFlags = MOUSEEVENTF_RIGHTUP;
        ++count;
    }
    if (count > 0)
        SendInput(static_cast<UINT>(count), inputs, sizeof(INPUT));
}

void VirtualMouse::SetConfig(Config config) noexcept {
    m_config = std::move(config);
}

const VirtualMouse::Config& VirtualMouse::GetConfig() const noexcept {
    return m_config;
}

void VirtualMouse::SetEnabled(bool enabled) noexcept {
    m_enabled = enabled;
    if (!enabled) {
        m_currentVelocity   = {};
        m_subPixelRemainder = {};
    }
}

bool VirtualMouse::IsEnabled() const noexcept {
    return m_enabled;
}

void VirtualMouse::Update(const ControllerState& state, float deltaSeconds) {
    if (!m_enabled || deltaSeconds <= 0.0f) return;

    const Vec2 raw    = m_config.useRightStick ? state.rightStick : state.leftStick;
    const Vec2 curved = ApplyCurve(raw);

    // Exponential smoothing toward target velocity
    const float safeDenom = m_config.accelerationMs * 0.001f + 1e-6f;
    const float alpha = (std::min)(1.0f, deltaSeconds / safeDenom);
    m_currentVelocity.x += (curved.x - m_currentVelocity.x) * alpha;
    m_currentVelocity.y += (curved.y - m_currentVelocity.y) * alpha;

    MoveCursor(m_currentVelocity, deltaSeconds);
    HandleClicks(state);
    HandleScroll(state, deltaSeconds);
}

Vec2 VirtualMouse::ApplyCurve(Vec2 raw) const noexcept {
    const float mag = std::sqrt(raw.x * raw.x + raw.y * raw.y);
    if (mag < 1e-6f) return {};
    const float curved = std::pow(mag, m_config.curveExponent);
    const float scale  = curved / mag * m_config.maxSpeedPx;
    return {raw.x * scale, raw.y * scale};
}

void VirtualMouse::MoveCursor(Vec2 velocity, float deltaSeconds) {
    m_subPixelRemainder.x += velocity.x * deltaSeconds;
    m_subPixelRemainder.y += velocity.y * deltaSeconds;

    const int32_t dx = static_cast<int32_t>(m_subPixelRemainder.x);
    const int32_t dy = static_cast<int32_t>(m_subPixelRemainder.y);
    if (dx == 0 && dy == 0) return;

    m_subPixelRemainder.x -= static_cast<float>(dx);
    m_subPixelRemainder.y -= static_cast<float>(dy);

    INPUT input      = {};
    input.type       = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    input.mi.dx      = dx;
    input.mi.dy      = dy;
    SendInput(1, &input, sizeof(INPUT));
}

void VirtualMouse::HandleClicks(const ControllerState& state) {
    if (!m_config.triggersAsClicks) return;

    constexpr float kClickThreshold = 0.5f;
    const bool wantLeft  = state.leftTrigger  >= kClickThreshold;
    const bool wantRight = state.rightTrigger >= kClickThreshold;

    INPUT inputs[2] = {};
    int   count     = 0;

    if (wantLeft != m_leftButtonDown) {
        inputs[count].type       = INPUT_MOUSE;
        inputs[count].mi.dwFlags = wantLeft ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        m_leftButtonDown         = wantLeft;
        ++count;
    }
    if (wantRight != m_rightButtonDown) {
        inputs[count].type       = INPUT_MOUSE;
        inputs[count].mi.dwFlags = wantRight ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
        m_rightButtonDown        = wantRight;
        ++count;
    }

    if (count > 0)
        SendInput(static_cast<UINT>(count), inputs, sizeof(INPUT));
}

void VirtualMouse::HandleScroll(const ControllerState& state, float deltaSeconds) {
    float scrollAxis = 0.0f;
    if (HasButton(state.buttons, Button::DPadUp))   scrollAxis =  1.0f;
    if (HasButton(state.buttons, Button::DPadDown))  scrollAxis = -1.0f;

    if (std::abs(scrollAxis) < 1e-6f) {
        m_scrollAccumulator = 0.0f;
        return;
    }

    if (m_config.invertScroll) scrollAxis = -scrollAxis;
    m_scrollAccumulator += scrollAxis * m_config.scrollSpeed * deltaSeconds;

    const int32_t lines = static_cast<int32_t>(m_scrollAccumulator);
    if (lines == 0) return;

    m_scrollAccumulator -= static_cast<float>(lines);

    INPUT input        = {};
    input.type         = INPUT_MOUSE;
    input.mi.dwFlags   = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = static_cast<DWORD>(lines * WHEEL_DELTA);
    SendInput(1, &input, sizeof(INPUT));
}

} // namespace enjoystick::cursor
