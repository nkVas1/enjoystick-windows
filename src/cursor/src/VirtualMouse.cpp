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
    m_config            = std::move(config);
    m_currentVelocity   = {};
    m_subPixelRemainder = {};
    m_scrollV           = 0.0f;
    m_scrollH           = 0.0f;
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
        m_scrollV           = 0.0f;
        m_scrollH           = 0.0f;
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
// Update  — 250 Hz polling thread
// ---------------------------------------------------------------------------

void VirtualMouse::Update(const ControllerState& state, float deltaSeconds) {
    if (!m_enabled) return;

    // LB held while in trigger-click mode = scroll wheel modifier:
    //   right stick Y → vertical scroll
    //   right stick X → horizontal scroll
    const bool lbHeld = HasButton(state.buttons, Button::LB);
    const bool scrollModActive = (m_config.triggersAsClicks && lbHeld);

    if (scrollModActive) {
        // Don't move cursor while scroll modifier is active
        m_currentVelocity   = {};
        m_subPixelRemainder = {};
        HandleScrollStick(state, deltaSeconds);
    } else {
        const Vec2 stick = m_config.useRightStick ? state.rightStick : state.leftStick;
        const Vec2 targetVelocity = ApplyCurve(stick);

        const float alpha = (m_config.accelerationMs > 0.0f)
            ? std::min(1.0f, deltaSeconds * 1000.0f / m_config.accelerationMs)
            : 1.0f;

        m_currentVelocity.x += (targetVelocity.x - m_currentVelocity.x) * alpha;
        m_currentVelocity.y += (targetVelocity.y - m_currentVelocity.y) * alpha;

        MoveCursor(m_currentVelocity, deltaSeconds);
        HandleScrollTrigger(state, deltaSeconds);
    }

    if (m_config.triggersAsClicks && !lbHeld) {
        HandleClicks(state);
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

Vec2 VirtualMouse::ApplyCurve(Vec2 raw) const noexcept {
    const float mag = std::sqrt(raw.x * raw.x + raw.y * raw.y);
    if (mag < 1e-5f) return {0.0f, 0.0f};
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
    m_subPixelRemainder.y -= static_cast<float>(dy);

    INPUT inp{};
    inp.type       = INPUT_MOUSE;
    inp.mi.dwFlags = MOUSEEVENTF_MOVE;
    inp.mi.dx      =  dx;
    inp.mi.dy      = -dy; // invert: stick up → cursor up
    SendInput(1, &inp, sizeof(INPUT));
}

void VirtualMouse::HandleClicks(const ControllerState& state) {
    constexpr float kThreshold = 0.5f;
    const bool wantLeft  = (state.rightTrigger >= kThreshold);
    const bool wantRight = (state.leftTrigger  >= kThreshold);

    auto sendMouse = [](DWORD flag) {
        INPUT inp{};
        inp.type       = INPUT_MOUSE;
        inp.mi.dwFlags = flag;
        SendInput(1, &inp, sizeof(INPUT));
    };

    if ( wantLeft  && !m_leftButtonDown)  { sendMouse(MOUSEEVENTF_LEFTDOWN);  m_leftButtonDown  = true;  }
    if (!wantLeft  &&  m_leftButtonDown)  { sendMouse(MOUSEEVENTF_LEFTUP);    m_leftButtonDown  = false; }
    if ( wantRight && !m_rightButtonDown) { sendMouse(MOUSEEVENTF_RIGHTDOWN); m_rightButtonDown = true;  }
    if (!wantRight &&  m_rightButtonDown) { sendMouse(MOUSEEVENTF_RIGHTUP);   m_rightButtonDown = false; }
}

void VirtualMouse::HandleScrollTrigger(const ControllerState& state, float deltaSeconds) {
    // Only used when triggersAsClicks = false
    if (m_config.triggersAsClicks) return;

    const bool lbHeld = HasButton(state.buttons, Button::LB);
    const float rawV = (state.leftTrigger - state.rightTrigger)
                     * m_config.scrollSpeed
                     * (m_config.invertScroll ? -1.0f : 1.0f);

    if (!lbHeld) {
        // Vertical scroll
        m_scrollV += rawV * deltaSeconds;
        const int ticksV = static_cast<int>(m_scrollV);
        if (ticksV != 0) {
            m_scrollV -= static_cast<float>(ticksV);
            INPUT inp{};
            inp.type         = INPUT_MOUSE;
            inp.mi.dwFlags   = MOUSEEVENTF_WHEEL;
            inp.mi.mouseData = static_cast<DWORD>(ticksV * WHEEL_DELTA);
            SendInput(1, &inp, sizeof(INPUT));
        }
    } else {
        // Horizontal scroll (LB held)
        m_scrollH += rawV * deltaSeconds;
        const int ticksH = static_cast<int>(m_scrollH);
        if (ticksH != 0) {
            m_scrollH -= static_cast<float>(ticksH);
            INPUT inp{};
            inp.type         = INPUT_MOUSE;
            inp.mi.dwFlags   = MOUSEEVENTF_HWHEEL;
            inp.mi.mouseData = static_cast<DWORD>(ticksH * WHEEL_DELTA);
            SendInput(1, &inp, sizeof(INPUT));
        }
    }
}

void VirtualMouse::HandleScrollStick(const ControllerState& state, float deltaSeconds) {
    // Called when triggersAsClicks=true AND LB is held
    // Right stick Y -> vertical scroll; right stick X -> horizontal scroll
    const Vec2& stick = state.rightStick;
    const float invMul = m_config.invertScroll ? -1.0f : 1.0f;

    // Vertical
    m_scrollV += stick.y * m_config.scrollSpeed * invMul * deltaSeconds;
    const int ticksV = static_cast<int>(m_scrollV);
    if (ticksV != 0) {
        m_scrollV -= static_cast<float>(ticksV);
        INPUT inp{};
        inp.type         = INPUT_MOUSE;
        inp.mi.dwFlags   = MOUSEEVENTF_WHEEL;
        inp.mi.mouseData = static_cast<DWORD>(ticksV * WHEEL_DELTA);
        SendInput(1, &inp, sizeof(INPUT));
    }

    // Horizontal
    m_scrollH += stick.x * m_config.scrollSpeed * invMul * deltaSeconds;
    const int ticksH = static_cast<int>(m_scrollH);
    if (ticksH != 0) {
        m_scrollH -= static_cast<float>(ticksH);
        INPUT inp{};
        inp.type         = INPUT_MOUSE;
        inp.mi.dwFlags   = MOUSEEVENTF_HWHEEL;
        inp.mi.mouseData = static_cast<DWORD>(ticksH * WHEEL_DELTA);
        SendInput(1, &inp, sizeof(INPUT));
    }
}

} // namespace enjoystick::cursor
