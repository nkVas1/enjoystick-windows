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
    static constexpr float kMinMovementPixels = 0.5f;  // sub-pixel accumulation threshold

    inline HMONITOR ToHMONITOR(uintptr_t v) noexcept {
        return reinterpret_cast<HMONITOR>(v);
    }
    inline uintptr_t FromHMONITOR(HMONITOR h) noexcept {
        return reinterpret_cast<uintptr_t>(h);
    }
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
        m_scrollHoldMs  = 0.0f;
        m_scrollActive  = false;
        m_ltWasDown = m_rtWasDown = false;
    }
}

bool VirtualMouse::IsEnabled() const noexcept { return m_enabled; }

// ---------------------------------------------------------------------------
// Adaptive per-monitor speed calibration  (v2)
// ---------------------------------------------------------------------------

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

    HMONITOR mon        = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
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

// ---------------------------------------------------------------------------
// Accelerate — per-axis curve.
// Input: normalised axis value in [0, 1].
// Returns: speed in px/ms along that axis.
//
// Design:
//   In the linearZone the response is proportional (precision mode).
//   Beyond it we apply the power curve, giving a smooth exponential feel
//   without the hard kink that a simple if/else threshold creates.
// ---------------------------------------------------------------------------
float VirtualMouse::Accelerate(float normalised) const noexcept {
    if (normalised <= 0.0f) return 0.0f;
    const float maxSpeed = EffectiveMaxSpeedPx();
    const float lz = m_config.linearZone;
    if (normalised <= lz) {
        // Linear zone: gentle precision mode
        return (normalised / std::max(lz, 0.01f)) * maxSpeed * 0.15f;
    }
    // Remap to [0,1] outside linear zone and apply power curve
    const float t = (normalised - lz) / (1.0f - lz + 1e-6f);
    return std::pow(t, m_config.curveExponent) * maxSpeed;
}

// ---------------------------------------------------------------------------
// Cross-axis suppression helper.
// When the dominant axis is strongly over crossAxisThreshold of the total
// magnitude, attenuate the minor axis to reduce drift during cardinal moves.
// ---------------------------------------------------------------------------
static void ApplyCrossAxisSuppression(
    float& x, float& y,
    float mag,
    float threshold,
    float damp) noexcept
{
    if (mag < 1e-6f || threshold >= 1.0f) return;
    const float ax = std::abs(x) / mag;
    const float ay = std::abs(y) / mag;
    if (ax > threshold)       y *= damp;
    else if (ay > threshold)  x *= damp;
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

    // Clamp to unit circle
    float nx = dx, ny = dy;
    if (mag > 1.0f) { nx /= mag; ny /= mag; }
    const float clampedMag = std::min(mag, 1.0f);

    // Cross-axis suppression on normalised components
    ApplyCrossAxisSuppression(
        nx, ny, clampedMag,
        m_config.crossAxisThreshold,
        m_config.crossAxisDamp);

    // Apply per-axis independent acceleration curve.
    // This avoids the diagonal speed boost that happens when a single
    // Accelerate(magnitude) is used — now each axis is curved independently.
    const float ramp = std::min(1.0f,
        deltaMs / std::max(1.0f, m_config.accelerationMs));

    const float speedX = Accelerate(std::abs(nx));
    const float speedY = Accelerate(std::abs(ny));

    const float moveX = (nx < 0 ? -1.0f : 1.0f) * speedX * deltaMs * (0.35f + 0.65f * ramp);
    const float moveY = -(ny < 0 ? -1.0f : 1.0f) * speedY * deltaMs * (0.35f + 0.65f * ramp);

    m_accumX += moveX;
    m_accumY += moveY;

    const long ix = static_cast<long>(m_accumX);
    const long iy = static_cast<long>(m_accumY);
    m_accumX -= static_cast<float>(ix);
    m_accumY -= static_cast<float>(iy);

    if (ix == 0 && iy == 0) return;

    if (m_config.wrapEdges) {
        POINT pt;
        GetCursorPos(&pt);
        // Use virtual screen bounds to support multi-monitor setups correctly.
        const int vsLeft  = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int vsTop   = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int vsW     = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const int vsH     = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (vsW > 0 && vsH > 0) {
            int nx2 = ((static_cast<int>(pt.x) - vsLeft + ix) % vsW + vsW) % vsW + vsLeft;
            int ny2 = ((static_cast<int>(pt.y) - vsTop  + iy) % vsH + vsH) % vsH + vsTop;
            SetCursorPos(nx2, ny2);
        }
    } else {
        PostMouseInput(ix, iy, MOUSEEVENTF_MOVE);
    }
}

void VirtualMouse::ScrollStick(float dy, float deltaMs) {
    if (!m_enabled) return;
    if (std::abs(dy) < 1e-6f) {
        // Stick released — reset scroll hold timer
        m_scrollAccum   = 0.0f;
        m_scrollHoldMs  = 0.0f;
        m_scrollActive  = false;
        return;
    }

    // Accumulate hold time for acceleration ramp
    m_scrollActive   = true;
    m_scrollHoldMs  += deltaMs;

    // Compute scroll acceleration multiplier.
    // Stays at 1.0x until scrollAccelStartMs, then ramps up to scrollSpeedMax
    // over scrollAccelRampMs using a smooth ease-in quad curve.
    float accelMult = 1.0f;
    const float startMs = m_config.scrollAccelStartMs;
    const float rampMs  = std::max(1.0f, m_config.scrollAccelRampMs);
    const float maxMult = m_config.scrollSpeedMax;
    if (m_scrollHoldMs > startMs) {
        const float t  = std::min(1.0f, (m_scrollHoldMs - startMs) / rampMs);
        accelMult = 1.0f + (maxMult - 1.0f) * (t * t);  // ease-in quad
    }

    m_scrollAccum += dy * m_config.scrollSpeed * accelMult * deltaMs;
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
