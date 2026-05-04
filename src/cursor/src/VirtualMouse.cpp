// WIN32_LEAN_AND_MEAN and NOMINMAX injected by CMake.
#include <Windows.h>
#include <ShellScalingApi.h>

#include "enjoystick/cursor/VirtualMouse.hpp"

#include <algorithm>
#include <cmath>

#pragma comment(lib, "Shcore.lib")

namespace enjoystick::cursor {

namespace {
    static constexpr float kReferenceWidthPx  = 1920.0f;
    static constexpr float kReferenceDpi      = 96.0f;
    static constexpr float kMinMovementPixels = 1.0f;

    inline HMONITOR ToHMONITOR(uintptr_t v) noexcept {
        return reinterpret_cast<HMONITOR>(v);
    }
    inline uintptr_t FromHMONITOR(HMONITOR h) noexcept {
        return reinterpret_cast<uintptr_t>(h);
    }
    // Suppress unused-function warning if optimiser inlines everything
    (void)&ToHMONITOR;
}

VirtualMouse::VirtualMouse(MouseConfig config)
    : m_config(config)
{
    RefreshMonitorProfileIfNeeded();
}

void VirtualMouse::SetConfig(MouseConfig config) {
    m_config = config;
    m_cachedMonitor = 0;
}

const MouseConfig& VirtualMouse::GetConfig() const noexcept { return m_config; }

void VirtualMouse::SetEnabled(bool enabled) noexcept {
    m_enabled = enabled;
    if (!enabled) {
        m_accumX = m_accumY = m_scrollAccum = 0.0f;
        m_ltWasDown = m_rtWasDown = false;
    }
}

bool VirtualMouse::IsEnabled() const noexcept { return m_enabled; }

// ---------------------------------------------------------------------------
// Adaptive per-monitor speed calibration  (v2)
//
// Target: crossing screen width at full stick deflection = targetTraversalMs.
// Formula:
//   resolutionFactor = widthPx / kReferenceWidthPx
//   dpiFactor        = dpiX    / kReferenceDpi
//   density          = lerp(resolutionFactor, dpiFactor, dpiWeight)
//   densityPenalty   = 1.0 / sqrt(density)
//   targetPxPerMs    = widthPx / targetTraversalMs
//   speedScale       = (targetPxPerMs / maxSpeedPx) * densityPenalty
//                      clamped to [adaptiveMinScale, adaptiveMaxScale]
//
// Resulting scales at baseSpeed=6.0, traversal=900ms:
//   1280x720  @96  dpi  -> ~0.72
//   1366x768  @96  dpi  -> ~0.77
//   1920x1080 @96  dpi  ->  1.00  (reference)
//   2560x1440 @96  dpi  -> ~1.30
//   3840x2160 @163 dpi  -> ~1.48
// ---------------------------------------------------------------------------

// NOTE: MonitorProfile is a namespace-scope struct, NOT a nested type of
// VirtualMouse. The return type must be fully qualified so the compiler can
// resolve it before entering VirtualMouse's scope (MSVC C2039 fix).
auto VirtualMouse::QueryActiveMonitorProfile() const noexcept -> MonitorProfile
{
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

    profile.scaleX     = profile.dpiX / kReferenceDpi;
    profile.scaleY     = profile.dpiY / kReferenceDpi;
    profile.approxPpi  = profile.dpiX;
    profile.speedScale = ComputeAdaptiveScale(profile);
    return profile;
}

float VirtualMouse::ComputeAdaptiveScale(
    const MonitorProfile& profile) const noexcept
{
    const float resolutionFactor = profile.widthPx / kReferenceWidthPx;
    const float dpiFactor        = profile.dpiX    / kReferenceDpi;

    const float w       = std::max(0.0f, std::min(1.0f, m_config.dpiWeight));
    const float density = resolutionFactor * (1.0f - w) + dpiFactor * w;
    const float penalty = 1.0f / std::sqrt(std::max(0.30f, density));

    const float traversalMs   = std::max(100.0f, m_config.targetTraversalMs);
    const float targetPxPerMs = profile.widthPx / traversalMs;
    const float baseSpeed     = std::max(0.25f, m_config.maxSpeedPx);
    const float rawScale      = (targetPxPerMs / baseSpeed) * penalty;

    return std::max(m_config.adaptiveMinScale,
           std::min(rawScale, m_config.adaptiveMaxScale));
}

void VirtualMouse::RefreshMonitorProfileIfNeeded() noexcept {
    POINT pt{};
    if (!GetCursorPos(&pt)) return;

    HMONITOR mon       = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    const uintptr_t key = FromHMONITOR(mon);
    if (key == m_cachedMonitor && m_monitorProfile.widthPx > 0.0f) return;

    m_cachedMonitor  = key;
    m_monitorProfile = QueryActiveMonitorProfile();
}

float VirtualMouse::EffectiveMaxSpeedPx() const noexcept {
    const float base = std::max(0.25f, m_config.maxSpeedPx);
    if (!m_config.adaptiveSpeed) return base;
    return base * m_monitorProfile.speedScale;
}

float VirtualMouse::Accelerate(float magnitude) const noexcept {
    if (magnitude <= 0.0f) return 0.0f;
    const float maxSpeed = EffectiveMaxSpeedPx();
    if (magnitude <= m_config.linearZone)
        return (magnitude / m_config.linearZone) * maxSpeed * 0.18f;
    return std::pow(magnitude, m_config.curveExponent) * maxSpeed;
}

void VirtualMouse::Update(const ControllerState& state, float deltaMs) {
    if (!m_enabled) return;

    const Vec2 stick = m_config.useRightStick ? state.rightStick : state.leftStick;
    Update(stick.x, stick.y, deltaMs);

    const float scrollY = m_config.useRightStick ? state.leftStick.y : state.rightStick.y;
    ScrollStick(scrollY, deltaMs);

    if (m_config.triggersAsClicks) {
        const bool ltDown = state.leftTrigger  > 0.5f;
        const bool rtDown = state.rightTrigger > 0.5f;
        if (ltDown && !m_ltWasDown) LeftDown();
        if (!ltDown && m_ltWasDown) LeftUp();
        if (rtDown && !m_rtWasDown) RightClick();
        m_ltWasDown = ltDown;
        m_rtWasDown = rtDown;
    }
}

void VirtualMouse::Update(float dx, float dy, float deltaMs) {
    if (!m_enabled || deltaMs <= 0.0f) return;

    RefreshMonitorProfileIfNeeded();

    const float mag = std::sqrt(dx * dx + dy * dy);
    if (mag < 1e-6f) { m_accumX = m_accumY = 0.0f; return; }

    const float speed = Accelerate(std::min(mag, 1.0f));
    const float nx    =  dx / mag;
    const float ny    = -dy / mag;

    const float ramp     = std::min(1.0f, deltaMs / std::max(1.0f, m_config.accelerationMs));
    const float movement = std::max(kMinMovementPixels,
        speed * deltaMs * (0.35f + 0.65f * ramp));

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
        const int sw = GetSystemMetrics(SM_CXSCREEN);
        const int sh = GetSystemMetrics(SM_CYSCREEN);
        SetCursorPos(
            (static_cast<int>(pt.x) + ix + sw) % sw,
            (static_cast<int>(pt.y) + iy + sh) % sh);
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

void VirtualMouse::LeftClick()   { if (!m_enabled) return; PostMouseInput(0,0,MOUSEEVENTF_LEFTDOWN);   PostMouseInput(0,0,MOUSEEVENTF_LEFTUP); }
void VirtualMouse::RightClick()  { if (!m_enabled) return; PostMouseInput(0,0,MOUSEEVENTF_RIGHTDOWN);  PostMouseInput(0,0,MOUSEEVENTF_RIGHTUP); }
void VirtualMouse::MiddleClick() { if (!m_enabled) return; PostMouseInput(0,0,MOUSEEVENTF_MIDDLEDOWN); PostMouseInput(0,0,MOUSEEVENTF_MIDDLEUP); }
void VirtualMouse::LeftDown()    { if (!m_enabled) return; PostMouseInput(0,0,MOUSEEVENTF_LEFTDOWN); }
void VirtualMouse::LeftUp()      { if (!m_enabled) return; PostMouseInput(0,0,MOUSEEVENTF_LEFTUP); }

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
