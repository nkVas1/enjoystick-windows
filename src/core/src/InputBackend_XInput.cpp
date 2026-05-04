#include "InputBackend_XInput.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>

#pragma comment(lib, "xinput.lib")

namespace enjoystick::core {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr float kS16Norm = 1.0f / 32767.0f;
static constexpr float kU8Norm  = 1.0f / 255.0f;
static constexpr WORD  kXInputGuideButton = 0x0400;

// ---------------------------------------------------------------------------
// RumbleStopTimer
//
// A Win32 ThreadpoolTimer that fires once after durationMs to stop
// vibration on the given XInput controller slot.  Self-deletes on fire.
// No class pointer captured — only primitive values.
// ---------------------------------------------------------------------------

namespace {

struct RumbleStopCtx {
    DWORD slotIndex;
};

static void CALLBACK RumbleStopCallback(
    PTP_CALLBACK_INSTANCE, void* ctx, PTP_TIMER timer) noexcept
{
    auto* r = static_cast<RumbleStopCtx*>(ctx);
    XINPUT_VIBRATION stop{};
    XInputSetState(r->slotIndex, &stop);
    CloseThreadpoolTimer(timer);
    delete r;
}

static void ScheduleRumbleStop(DWORD slotIndex, DWORD durationMs) noexcept {
    auto* ctx = new (std::nothrow) RumbleStopCtx{slotIndex};
    if (!ctx) return;

    PTP_TIMER timer = CreateThreadpoolTimer(RumbleStopCallback, ctx, nullptr);
    if (!timer) { delete ctx; return; }

    ULARGE_INTEGER due;
    due.QuadPart = static_cast<ULONGLONG>(
        -static_cast<LONGLONG>(durationMs) * 10'000LL);
    FILETIME ft;
    ft.dwLowDateTime  = due.LowPart;
    ft.dwHighDateTime = due.HighPart;
    SetThreadpoolTimer(timer, &ft, 0, 0);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// BuildState
// ---------------------------------------------------------------------------

static ControllerState BuildState(
    ControllerId          id,
    const XINPUT_GAMEPAD& gp,
    bool                  hasGuide) noexcept
{
    ControllerState s;
    s.id = id;

    auto map = [&](WORD mask, Button btn) {
        if (gp.wButtons & mask) s.buttons = s.buttons | btn;
    };
    map(XINPUT_GAMEPAD_A,              Button::South);
    map(XINPUT_GAMEPAD_B,              Button::East);
    map(XINPUT_GAMEPAD_X,              Button::West);
    map(XINPUT_GAMEPAD_Y,              Button::North);
    map(XINPUT_GAMEPAD_LEFT_SHOULDER,  Button::LB);
    map(XINPUT_GAMEPAD_RIGHT_SHOULDER, Button::RB);
    map(XINPUT_GAMEPAD_START,          Button::Start);
    map(XINPUT_GAMEPAD_BACK,           Button::Select);
    map(XINPUT_GAMEPAD_LEFT_THUMB,     Button::LS);
    map(XINPUT_GAMEPAD_RIGHT_THUMB,    Button::RS);
    map(XINPUT_GAMEPAD_DPAD_UP,        Button::DPadUp);
    map(XINPUT_GAMEPAD_DPAD_DOWN,      Button::DPadDown);
    map(XINPUT_GAMEPAD_DPAD_LEFT,      Button::DPadLeft);
    map(XINPUT_GAMEPAD_DPAD_RIGHT,     Button::DPadRight);

    if (gp.bLeftTrigger  > 128) s.buttons = s.buttons | Button::LT_Click;
    if (gp.bRightTrigger > 128) s.buttons = s.buttons | Button::RT_Click;

    if (hasGuide && (gp.wButtons & kXInputGuideButton))
        s.buttons = s.buttons | Button::Guide;

    s.leftStick.x  = std::clamp(static_cast<float>(gp.sThumbLX) * kS16Norm, -1.0f, 1.0f);
    s.leftStick.y  = std::clamp(static_cast<float>(gp.sThumbLY) * kS16Norm, -1.0f, 1.0f);
    s.rightStick.x = std::clamp(static_cast<float>(gp.sThumbRX) * kS16Norm, -1.0f, 1.0f);
    s.rightStick.y = std::clamp(static_cast<float>(gp.sThumbRY) * kS16Norm, -1.0f, 1.0f);

    s.leftTrigger  = static_cast<float>(gp.bLeftTrigger)  * kU8Norm;
    s.rightTrigger = static_cast<float>(gp.bRightTrigger) * kU8Norm;

    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    s.timestamp = static_cast<uint64_t>(qpc.QuadPart);

    return s;
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

XInputBackend::XInputBackend(Config cfg, DeadzoneFilter filter)
    : m_config(std::move(cfg))
    , m_deadzone(std::move(filter))
{
    LoadXInputDll();
}

XInputBackend::~XInputBackend() {
    Stop();
    UnloadXInputDll();
}

// ---------------------------------------------------------------------------
// XInput DLL dynamic loading
// ---------------------------------------------------------------------------

void XInputBackend::LoadXInputDll() {
    static const wchar_t* const kLibs[] = { L"xinput1_4.dll", L"xinput1_3.dll" };
    for (const wchar_t* name : kLibs) {
        m_xinputModule = LoadLibraryW(name);
        if (m_xinputModule) break;
    }
    if (!m_xinputModule)
        m_xinputModule = LoadLibraryW(L"xinput9_1_0.dll");

    if (m_xinputModule) {
        m_getStateEx = reinterpret_cast<PFN_XInputGetStateEx>(
            GetProcAddress(m_xinputModule, reinterpret_cast<LPCSTR>(100)));
    }
}

void XInputBackend::UnloadXInputDll() {
    m_getStateEx = nullptr;
    if (m_xinputModule) {
        FreeLibrary(m_xinputModule);
        m_xinputModule = nullptr;
    }
}

DWORD XInputBackend::DoGetState(uint32_t index, XINPUT_STATE* pState) const {
    if (m_getStateEx)
        return m_getStateEx(static_cast<DWORD>(index), pState);
    return XInputGetState(static_cast<DWORD>(index), pState);
}

// ---------------------------------------------------------------------------
// Start / Stop
// ---------------------------------------------------------------------------

void XInputBackend::Start() {
    if (m_running.exchange(true)) return;
    m_pollThread = std::thread([this] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        PollLoop();
    });
}

void XInputBackend::Stop() {
    if (!m_running.exchange(false)) return;
    if (m_pollThread.joinable()) m_pollThread.join();
}

// ---------------------------------------------------------------------------
// Polling loop
// ---------------------------------------------------------------------------

void XInputBackend::PollLoop() {
    using Clock = std::chrono::high_resolution_clock;
    const auto interval = std::chrono::microseconds(
        static_cast<long long>(1'000'000.0 / m_config.pollingRateHz));
    auto nextTick = Clock::now();

    while (m_running.load(std::memory_order_relaxed)) {
        for (uint32_t i = 0; i < XUSER_MAX_COUNT; ++i)
            PollController(i);
        nextTick += interval;
        std::this_thread::sleep_until(nextTick);
    }
}

void XInputBackend::PollController(uint32_t index) {
    XINPUT_STATE xi{};
    const DWORD result = DoGetState(index, &xi);

    std::unique_lock lock(m_stateMutex);
    Slot& slot = m_slots[index];

    if (result == ERROR_SUCCESS) {
        if (!slot.connected) {
            slot.connected = true;
            slot.packetNum = xi.dwPacketNumber;
            slot.state     = {};
            lock.unlock();
            FireConnection(static_cast<ControllerId>(index), ConnectionEvent::Connected);
            return;
        }

        if (xi.dwPacketNumber == slot.packetNum) return;
        slot.packetNum = xi.dwPacketNumber;

        const bool guideAvailable = (m_getStateEx != nullptr);
        ControllerState newState = BuildState(
            static_cast<ControllerId>(index), xi.Gamepad, guideAvailable);

        const uint32_t prev = static_cast<uint32_t>(slot.state.buttons);
        const uint32_t curr = static_cast<uint32_t>(newState.buttons);
        newState.buttonsDown = static_cast<Button>(curr & ~prev);
        newState.buttonsUp   = static_cast<Button>(prev & ~curr);

        newState.leftStick  = m_deadzone.Apply(newState.leftStick);
        newState.rightStick = m_deadzone.Apply(newState.rightStick);

        slot.state = newState;
        lock.unlock();
        FireInput(newState);

    } else {
        if (slot.connected) {
            slot.connected = false;
            slot.state     = {};
            lock.unlock();
            FireConnection(static_cast<ControllerId>(index), ConnectionEvent::Disconnected);
        }
    }
}

// ---------------------------------------------------------------------------
// Callback registration
// ---------------------------------------------------------------------------

CallbackHandle XInputBackend::OnInput(InputEventCallback cb) {
    std::unique_lock lock(m_callbackMutex);
    const uint64_t id = ++m_nextCallbackId;
    m_inputCallbacks.push_back({id, std::move(cb)});
    return CallbackHandle([this, id] {
        std::unique_lock lk(m_callbackMutex);
        for (auto& e : m_inputCallbacks)
            if (e.id == id) { e.fn = nullptr; return; }
    });
}

CallbackHandle XInputBackend::OnConnection(ConnectionCallback cb) {
    std::unique_lock lock(m_callbackMutex);
    const uint64_t id = ++m_nextCallbackId;
    m_connCallbacks.push_back({id, std::move(cb)});
    return CallbackHandle([this, id] {
        std::unique_lock lk(m_callbackMutex);
        for (auto& e : m_connCallbacks)
            if (e.id == id) { e.fn = nullptr; return; }
    });
}

void XInputBackend::FireInput(const ControllerState& state) {
    std::shared_lock lock(m_callbackMutex);
    for (const auto& e : m_inputCallbacks)
        if (e.fn) e.fn(state);
}

void XInputBackend::FireConnection(ControllerId id, ConnectionEvent ev) {
    std::shared_lock lock(m_callbackMutex);
    for (const auto& e : m_connCallbacks)
        if (e.fn) e.fn(id, ev);
}

// ---------------------------------------------------------------------------
// Haptics — Rumble
// ---------------------------------------------------------------------------

void XInputBackend::Rumble(ControllerId id, RumbleParams params) {
    if (id >= XUSER_MAX_COUNT) return;
    if (!m_config.hapticsEnabled) return;

    XINPUT_VIBRATION vib{};
    vib.wLeftMotorSpeed  = static_cast<WORD>(
        std::clamp(params.lowFreqMotor,  0.0f, 1.0f) * 65535.0f);
    vib.wRightMotorSpeed = static_cast<WORD>(
        std::clamp(params.highFreqMotor, 0.0f, 1.0f) * 65535.0f);
    XInputSetState(id, &vib);

    if (params.durationMs > 0)
        ScheduleRumbleStop(static_cast<DWORD>(id), params.durationMs);
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

std::vector<ControllerInfo> XInputBackend::GetConnectedControllers() const {
    std::shared_lock lock(m_stateMutex);
    std::vector<ControllerInfo> out;
    out.reserve(XUSER_MAX_COUNT);
    for (uint32_t i = 0; i < XUSER_MAX_COUNT; ++i) {
        if (m_slots[i].connected) {
            out.push_back({
                static_cast<ControllerId>(i),
                ControllerType::Xbox,
                std::string("Xbox Controller #") + std::to_string(i)
            });
        }
    }
    return out;
}

ControllerState XInputBackend::GetState(ControllerId id) const {
    if (id >= XUSER_MAX_COUNT) return {};
    std::shared_lock lock(m_stateMutex);
    return m_slots[id].state;
}

} // namespace enjoystick::core
