// NOTE: WIN32_LEAN_AND_MEAN and NOMINMAX are injected by CMake.
#include <enjoystick/cursor/VirtualMouse.hpp>

#include <Windows.h>

#include <algorithm>
#include <cmath>

namespace enjoystick::cursor {

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

VirtualMouse::VirtualMouse(Config config)
    : m_config(std::move(config))
{}

VirtualMouse::~VirtualMouse() {
    // Release any held mouse buttons to avoid stuck clicks on shutdown
    auto release = [](DWORD flag) {
        INPUT inp{};
        inp.type       = INPUT_MOUSE;
        inp.mi.dwFlags = flag;
        SendInput(1, &inp, sizeof(INPUT));
    };
    if (m_leftButtonDown)  release(MOUSEEVENTF_LEFTUP);
    if (m_rightButtonDown) release(MOUSEEVENTF_RIGHTUP);
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void VirtualMouse::SetConfig(Config config) noexcept {
    m_config              = std::move(config);
    m_currentVelocity     = {};
    m_subPixelRemainder   = {};
    m_scrollAccumulator   = 0.0f;
}

const VirtualMouse::Config& VirtualMouse::GetConfig() const noexcept {
    return m_config;
}

void VirtualMouse::SetEnabled(bool enabled) noexcept {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (!enabled) {
        m_currentVelocity   = {};
        m_subPixelRemainder = {};
        m_scrollAccumulator = 0.0f;
        // Release any held clicks so buttons don’t get stuck
        auto release = [](DWORD flag) {
            INPUT inp{};
            inp.type       = INPUT_MOUSE;
            inp.mi.dwFlags = flag;
            SendInput(1, &inp, sizeof(INPUT));
        };
        if (m_leftButtonDown)  { release(MOUSEEVENTF_LEFTUP);  m_leftButtonDown  = false; }
        if (m_rightButtonDown) { release(MOUSEEVENTF_RIGHTUP); m_rightButtonDown = false; }
    }
}

bool VirtualMouse::IsEnabled() const noexcept { return m_enabled; }

// ---------------------------------------------------------------------------
// Update  — called from the input thread at polling rate (e.g. 250 Hz)
// ---------------------------------------------------------------------------

void VirtualMouse::Update(const ControllerState& state, float deltaSeconds) {
    if (!m_enabled) return;

    const Vec2 stick = m_config.useRightStick ? state.rightStick : state.leftStick;

    const Vec2 targetVelocity = ApplyCurve(stick);

    // Smooth interpolation: alpha = 1 → instant; smaller = smoother ramp
    const float alpha = (m_config.accelerationMs > 0.0f)
        ? std::min(1.0f, deltaSeconds * 1000.0f / m_config.accelerationMs)
        : 1.0f;

    m_currentVelocity.x += (targetVelocity.x - m_currentVelocity.x) * alpha;
    m_currentVelocity.y += (targetVelocity.y - m_currentVelocity.y) * alpha;

    MoveCursor(m_currentVelocity, deltaSeconds);
    HandleScroll(state, deltaSeconds);
    if (m_config.triggersAsClicks) HandleClicks(state);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

Vec2 VirtualMouse::ApplyCurve(Vec2 raw) const noexcept {
    const float mag = std::sqrt(raw.x * raw.x + raw.y * raw.y);
    if (mag < 1e-5f) return {0.0f, 0.0f};

    // speed = maxSpeedPx * mag^exponent
    const float speed = m_config.maxSpeedPx * std::pow(mag, m_config.curveExponent);
    return { (raw.x / mag) * speed, (raw.y / mag) * speed };
}

void VirtualMouse::MoveCursor(Vec2 velocity, float deltaSeconds) {
    m_subPixelRemainder.x += velocity.x * deltaSeconds;
    m_subPixelRemainder.y += velocity.y * deltaSeconds;

    const int dx = static_cast<int>(m_subPixelRemainder.x);
    const int dy = static_cast<int>(m_subPixelRemainder.y);

    if (dx == 0 && dy == 0) return;

    m_subPixelRemainder.x -= static_cast<float>(dx);
    // Y axis: positive stick = up on screen, but Win32 +dy = down; invert.
    m_subPixelRemainder.y -= static_cast<float>(dy);

    INPUT inp{};
    inp.type       = INPUT_MOUSE;
    inp.mi.dwFlags = MOUSEEVENTF_MOVE;
    inp.mi.dx      =  dx;
    inp.mi.dy      = -dy;  // invert Y: stick up → cursor up
    SendInput(1, &inp, sizeof(INPUT));
}

void VirtualMouse::HandleClicks(const ControllerState& state) {
    // Right trigger → left mouse button
    // Left  trigger → right mouse button
    constexpr float kThreshold = 0.5f;

    const bool wantLeft  = (state.rightTrigger >= kThreshold);
    const bool wantRight = (state.leftTrigger  >= kThreshold);

    auto sendMouse = [](DWORD flag) {
        INPUT inp{};
        inp.type       = INPUT_MOUSE;
        inp.mi.dwFlags = flag;
        SendInput(1, &inp, sizeof(INPUT));
    };

    if (wantLeft  && !m_leftButtonDown)  { sendMouse(MOUSEEVENTF_LEFTDOWN);  m_leftButtonDown  = true;  }
    if (!wantLeft  && m_leftButtonDown)  { sendMouse(MOUSEEVENTF_LEFTUP);    m_leftButtonDown  = false; }
    if (wantRight && !m_rightButtonDown) { sendMouse(MOUSEEVENTF_RIGHTDOWN); m_rightButtonDown = true;  }
    if (!wantRight && m_rightButtonDown) { sendMouse(MOUSEEVENTF_RIGHTUP);   m_rightButtonDown = false; }
}

void VirtualMouse::HandleScroll(const ControllerState& state, float deltaSeconds) {
    // Right trigger = scroll down, left trigger = scroll up
    // (when triggersAsClicks is enabled, triggers are used for clicks instead
    //  — don’t double-count them)
    if (m_config.triggersAsClicks) return;

    const float raw = (state.leftTrigger - state.rightTrigger)  // +1 = up
                    * m_config.scrollSpeed
                    * (m_config.invertScroll ? -1.0f : 1.0f);

    m_scrollAccumulator += raw * deltaSeconds;

    const int ticks = static_cast<int>(m_scrollAccumulator);
    if (ticks == 0) return;
    m_scrollAccumulator -= static_cast<float>(ticks);

    INPUT inp{};
    inp.type         = INPUT_MOUSE;
    inp.mi.dwFlags   = MOUSEEVENTF_WHEEL;
    inp.mi.mouseData = static_cast<DWORD>(ticks * WHEEL_DELTA);
    SendInput(1, &inp, sizeof(INPUT));
}

} // namespace enjoystick::cursor
