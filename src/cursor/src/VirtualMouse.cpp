// WIN32_LEAN_AND_MEAN and NOMINMAX injected by CMake.
#include <Windows.h>
#include <ShellScalingApi.h>

#include "enjoystick/cursor/VirtualMouse.hpp"

#include <algorithm>
#include <cmath>

#pragma comment(lib, "Shcore.lib")

namespace enjoystick::cursor {

namespace {
static constexpr float kReferenceWidthPx   = 1920.0f;
static constexpr float kReferenceHeightPx  = 1080.0f;
static constexpr float kReferenceDpi       = 96.0f;
static constexpr float kReferencePpi       = 96.0f;
static constexpr float kMinMovementPixels  = 1.0f;
}

VirtualMouse::VirtualMouse(MouseConfig config)
    : m_config(config)
{
    RefreshMonitorProfileIfNeeded();
}

void VirtualMouse::SetConfig(MouseConfig config) {
    m_config = config;
    RefreshMonitorProfileIfNeeded();
}
const MouseConfig& VirtualMouse::GetConfig() const noexcept { return m_config; }

void VirtualMouse::SetEnabled(bool enabled) noexcept {
    m_enabled = enabled;
    if (!enabled) {
        m_accumX = 0.0f;
        m_accumY = 0.0f;
        m_scrollAccum = 0.0f;
        m_ltWasDown = false;
        m_rtWasDown = false;
    }
}

bool VirtualMouse::IsEnabled() const noexcept { return m_enabled; }

void VirtualMouse::Update(const ControllerState& state, float deltaMs) {
    if (!m_enabled) return;

    const Vec2 stick = m_config.useRightStick ? state.rightStick : state.leftStick;
    Update(stick.x, stick.y, deltaMs);

    const float scrollY = m_config.useRightStick ? state.leftStick.y : state.rightStick.y;
    ScrollStick(scrollY, deltaMs);

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

float VirtualMouse::Accelerate(float magnitude) const noexcept {
    if (magnitude <= 0.0f) return 0.0f;
    const float maxSpeed = EffectiveMaxSpeedPx();
    if (magnitude <= m_config.linearZone)
        return magnitude * maxSpeed;
    return std::pow(magnitude, m_config.curveExponent) * maxSpeed;
}

float VirtualMouse::EffectiveMaxSpeedPx() const noexcept {
    const float baseSpeed = std::max(0.25f, m_config.maxSpeedPx);
    if (!m_config.adaptiveSpeed) return baseSpeed;
    return baseSpeed * std::max(m_config.adaptiveMinScale,
        std::min(m_monitorProfile.speedScale, m_config.adaptiveMaxScale));
}

VirtualMouse::MonitorProfile VirtualMouse::QueryActiveMonitorProfile() const noexcept {
    MonitorProfile profile;

    POINT pt{};
    if (!GetCursorPos(&pt)) return profile;

    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        const LONG w = mi.rcMonitor.right  - mi.rcMonitor.left;
        const LONG h = mi.rcMonitor.bottom - mi.rcMonitor.top;
        if (w > 0) profile.widthPx  = static_cast<float>(w);
        if (h > 0) profile.heightPx = static_cast<float>(h);
    }

    UINT dpiX = 96, dpiY = 96;
    if (SUCCEEDED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
        profile.dpiX = static_cast<float>(dpiX);
        profile.dpiY = static_cast<float>(dpiY);
    }

    profile.scaleX = profile.dpiX / kReferenceDpi;
    profile.scaleY = profile.dpiY / kReferenceDpi;

    const float diagPx = std::sqrt(profile.widthPx * profile.widthPx +
                                   profile.heightPx * profile.heightPx);
    const float refDiagPx = std::sqrt(kReferenceWidthPx * kReferenceWidthPx +
                                      kReferenceHeightPx * kReferenceHeightPx);
    const float densityFromResolution = std::max(0.55f, diagPx / refDiagPx);
    const float densityFromDpi = std::max(0.55f,
        ((profile.dpiX + profile.dpiY) * 0.5f) / kReferenceDpi);
    profile.approxPpi = kReferencePpi * densityFromDpi;

    const float targetPixelsPerSec = profile.widthPx *
        std::max(0.12f, std::min(m_config.targetScreenFracPerSec, 0.40f));
    const float targetPixelsPerMs = targetPixelsPerSec / 1000.0f;

    const float densityPenalty = 1.0f /
        std::sqrt(std::max(0.60f, densityFromResolution * 0.55f + densityFromDpi * 0.45f));
    const float rawScale = (targetPixelsPerMs / std::max(0.25f, m_config.maxSpeedPx)) * densityPenalty;

    profile.speedScale = std::max(m_config.adaptiveMinScale,
        std::min(rawScale, m_config.adaptiveMaxScale));
    return profile;
}

float VirtualMouse::ComputeAdaptiveScale(const MonitorProfile& profile) const noexcept {
    return std::max(m_config.adaptiveMinScale,
        std::min(profile.speedScale, m_config.adaptiveMaxScale));
}

void VirtualMouse::RefreshMonitorProfileIfNeeded() noexcept {
    POINT pt{};
    if (!GetCursorPos(&pt)) return;
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    if (mon != m_cachedMonitor) {
        m_cachedMonitor = mon;
        m_monitorProfile = QueryActiveMonitorProfile();
        m_monitorProfile.speedScale = ComputeAdaptiveScale(m_monitorProfile);
    } else if (m_monitorProfile.widthPx <= 0.0f || m_monitorProfile.heightPx <= 0.0f) {
        m_monitorProfile = QueryActiveMonitorProfile();
        m_monitorProfile.speedScale = ComputeAdaptiveScale(m_monitorProfile);
    }
}

void VirtualMouse::Update(float dx, float dy, float deltaMs) {
    if (!m_enabled || deltaMs <= 0.0f) return;

    RefreshMonitorProfileIfNeeded();

    const float mag = std::sqrt(dx * dx + dy * dy);
    if (mag < 1e-6f) {
        m_accumX = m_accumY = 0.0f;
        return;
    }

    const float speed = Accelerate(std::min(mag, 1.0f));
    const float nx    = dx / mag;
    const float ny    = -dy / mag;

    const float ramp = std::min(1.0f, deltaMs / std::max(1.0f, m_config.accelerationMs));
    const float movement = std::max(kMinMovementPixels, speed * deltaMs * (0.35f + 0.65f * ramp));

    m_accumX += nx * movement;
    m_accumY += ny * movement;

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

void VirtualMouse::ScrollStick(float dy, float deltaMs) {
    if (!m_enabled) return;
    if (std::abs(dy) < 1e-6f) { m_scrollAccum = 0.0f; return; }

    m_scrollAccum += dy * m_config.scrollSpeed * deltaMs;

    const long ticks = static_cast<long>(m_scrollAccum / WHEEL_DELTA);
    if (ticks == 0) return;

    m_scrollAccum -= static_cast<float>(ticks * WHEEL_DELTA);
    PostMouseInput(0, 0, MOUSEEVENTF_WHEEL, static_cast<int>(ticks * WHEEL_DELTA));
}

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
