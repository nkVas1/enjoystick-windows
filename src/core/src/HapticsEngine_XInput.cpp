// WIN32_LEAN_AND_MEAN and NOMINMAX injected by CMake.
#include <Windows.h>
#include <Xinput.h>
#pragma comment(lib, "xinput.lib")

#include <enjoystick/core/HapticsEngine.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <new>

namespace enjoystick::core {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

/// Send a vibration command directly to XInput.
static void XInputRumble(ControllerId id,
                         float low, float high,
                         float masterIntensity) noexcept
{
    if (id >= XUSER_MAX_COUNT) return;
    const float scale = std::clamp(masterIntensity, 0.0f, 1.0f);
    XINPUT_VIBRATION vib{};
    vib.wLeftMotorSpeed  = static_cast<WORD>(std::clamp(low,  0.0f, 1.0f) * scale * 65535.0f);
    vib.wRightMotorSpeed = static_cast<WORD>(std::clamp(high, 0.0f, 1.0f) * scale * 65535.0f);
    XInputSetState(static_cast<DWORD>(id), &vib);
}

/// Stop vibration on a given slot after durationMs (ThreadpoolTimer, no detach).
struct StopCtx { DWORD slot; };

static void CALLBACK StopCallback(
    PTP_CALLBACK_INSTANCE, void* ctx, PTP_TIMER timer) noexcept
{
    auto* s = static_cast<StopCtx*>(ctx);
    XINPUT_VIBRATION stop{};
    XInputSetState(s->slot, &stop);
    CloseThreadpoolTimer(timer);
    delete s;
}

static void ScheduleStop(ControllerId id, uint32_t durationMs) noexcept {
    auto* ctx  = new (std::nothrow) StopCtx{static_cast<DWORD>(id)};
    if (!ctx) return;
    PTP_TIMER t = CreateThreadpoolTimer(StopCallback, ctx, nullptr);
    if (!t) { delete ctx; return; }
    ULARGE_INTEGER due;
    due.QuadPart = static_cast<ULONGLONG>(
        -static_cast<LONGLONG>(durationMs) * 10'000LL);
    FILETIME ft{ due.LowPart, due.HighPart };
    SetThreadpoolTimer(t, &ft, 0, 0);
}

/// Schedule a follow-up rumble pulse deferredMs in the future.
struct PulseCtx {
    DWORD    slot;
    float    low;
    float    high;
    float    masterIntensity;
    uint32_t durationMs;
};

static void CALLBACK PulseCallback(
    PTP_CALLBACK_INSTANCE, void* ctx, PTP_TIMER timer) noexcept
{
    auto* p = static_cast<PulseCtx*>(ctx);
    XInputRumble(static_cast<ControllerId>(p->slot),
                 p->low, p->high, p->masterIntensity);
    ScheduleStop(static_cast<ControllerId>(p->slot), p->durationMs);
    CloseThreadpoolTimer(timer);
    delete p;
}

static void SchedulePulse(ControllerId id,
                          float low, float high, float master,
                          uint32_t durationMs, uint32_t deferredMs) noexcept
{
    auto* ctx  = new (std::nothrow) PulseCtx{static_cast<DWORD>(id),
                                              low, high, master, durationMs};
    if (!ctx) return;
    PTP_TIMER t = CreateThreadpoolTimer(PulseCallback, ctx, nullptr);
    if (!t) { delete ctx; return; }
    ULARGE_INTEGER due;
    due.QuadPart = static_cast<ULONGLONG>(
        -static_cast<LONGLONG>(deferredMs) * 10'000LL);
    FILETIME ft{ due.LowPart, due.HighPart };
    SetThreadpoolTimer(t, &ft, 0, 0);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// XInputHapticsEngine
// ---------------------------------------------------------------------------

class XInputHapticsEngine final : public HapticsEngine {
public:
    XInputHapticsEngine() = default;

    void Play(ControllerId id, HapticPattern pattern, float intensity) override {
        const float m = m_masterIntensity.load(std::memory_order_relaxed)
                        * std::clamp(intensity, 0.0f, 1.0f);

        switch (pattern) {

        case HapticPattern::Click:
            // Short high-freq snap
            XInputRumble(id, 0.0f, 0.70f, m);
            ScheduleStop(id, 30);
            break;

        case HapticPattern::SoftBump:
            XInputRumble(id, 0.30f, 0.20f, m);
            ScheduleStop(id, 50);
            break;

        case HapticPattern::DoubleTap:
            XInputRumble(id, 0.0f, 0.65f, m);
            ScheduleStop(id, 60);
            SchedulePulse(id, 0.0f, 0.55f, m, 60, 120);
            break;

        case HapticPattern::Error:
            // Three pulses: 40 ms on, 40 ms off, repeated three times
            XInputRumble(id, 0.60f, 0.30f, m);
            ScheduleStop(id, 40);
            SchedulePulse(id, 0.60f, 0.30f, m, 40, 80);
            SchedulePulse(id, 0.60f, 0.30f, m, 40, 160);
            break;

        case HapticPattern::LongPress:
            XInputRumble(id, 0.50f, 0.10f, m);
            ScheduleStop(id, 350);
            break;

        case HapticPattern::Custom:
            // Custom: caller should use Rumble() directly.
            break;
        }
    }

    void Rumble(ControllerId id, RumbleParams params) override {
        const float m = m_masterIntensity.load(std::memory_order_relaxed);
        XInputRumble(id, params.lowFreqMotor, params.highFreqMotor, m);
        if (params.durationMs > 0)
            ScheduleStop(id, params.durationMs);
    }

    void Cancel(ControllerId id) override {
        if (id >= XUSER_MAX_COUNT) return;
        XINPUT_VIBRATION stop{};
        XInputSetState(static_cast<DWORD>(id), &stop);
    }

    void SetMasterIntensity(float intensity) override {
        m_masterIntensity.store(
            std::clamp(intensity, 0.0f, 1.0f),
            std::memory_order_relaxed);
    }

private:
    std::atomic<float> m_masterIntensity{1.0f};
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::unique_ptr<HapticsEngine> HapticsEngine::Create() {
    return std::make_unique<XInputHapticsEngine>();
}

} // namespace enjoystick::core
