#include "InputBackend_XInput.hpp"

#include <chrono>
#include <algorithm>
#include <stdexcept>

#pragma comment(lib, "xinput.lib")

namespace enjoystick::core {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static constexpr float kU8Norm  = 1.0f / 255.0f;
static constexpr float kS16Norm = 1.0f / 32767.0f;

/// Map XInput XINPUT_STATE to our ControllerState.
static ControllerState BuildState(ControllerId id, const XINPUT_STATE& xi) noexcept {
    ControllerState s;
    s.id = id;

    const XINPUT_GAMEPAD& gp = xi.Gamepad;

    // Buttons
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

    // Analog sticks (raw [-1, 1] — deadzone applied later by DeadzoneFilter)
    s.leftStick.x  =  static_cast<float>(gp.sThumbLX) * kS16Norm;
    s.leftStick.y  =  static_cast<float>(gp.sThumbLY) * kS16Norm;
    s.rightStick.x =  static_cast<float>(gp.sThumbRX) * kS16Norm;
    s.rightStick.y =  static_cast<float>(gp.sThumbRY) * kS16Norm;

    // Clamp to [-1, 1] (sThumb can return -32768)
    auto clamp1 = [](float v) { return std::clamp(v, -1.0f, 1.0f); };
    s.leftStick.x  = clamp1(s.leftStick.x);
    s.leftStick.y  = clamp1(s.leftStick.y);
    s.rightStick.x = clamp1(s.rightStick.x);
    s.rightStick.y = clamp1(s.rightStick.y);

    // Triggers [0, 1]
    s.leftTrigger  = static_cast<float>(gp.bLeftTrigger)  * kU8Norm;
    s.rightTrigger = static_cast<float>(gp.bRightTrigger) * kU8Norm;

    // Timestamp
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    s.timestamp = static_cast<uint64_t>(qpc.QuadPart);

    return s;
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

XInputBackend::XInputBackend(const InputEngine::Config& cfg, DeadzoneFilter dz)
    : m_config(cfg), m_deadzone(std::move(dz)) {}

XInputBackend::~XInputBackend() {
    Stop();
}

// ---------------------------------------------------------------------------
// Start / Stop
// ---------------------------------------------------------------------------

void XInputBackend::Start() {
    if (m_running.exchange(true)) return; // already running

    m_pollThread = std::thread([this] {
        // Elevate thread priority for minimal latency
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
    using Clock    = std::chrono::high_resolution_clock;
    using Duration = std::chrono::duration<double>;

    const auto intervalUs = std::chrono::microseconds(
        static_cast<long long>(1'000'000.0 / m_config.pollingRateHz));

    auto nextTick = Clock::now();

    while (m_running.load(std::memory_order_relaxed)) {
        for (uint32_t i = 0; i < kMaxControllers; ++i) {
            PollController(i);
        }

        nextTick += intervalUs;
        std::this_thread::sleep_until(nextTick);
    }
}

void XInputBackend::PollController(uint32_t index) {
    XINPUT_STATE xi{};
    const DWORD result = XInputGetState(index, &xi);

    std::unique_lock lock(m_stateMutex);
    auto& slot = m_slots[index];

    if (result == ERROR_SUCCESS) {
        if (!slot.connected) {
            slot.connected = true;
            slot.packetNum = xi.dwPacketNumber;
            lock.unlock();
            FireConnection(static_cast<ControllerId>(index), ConnectionEvent::Connected);
            return;
        }

        // Only process if packet changed (XInput optimization)
        if (xi.dwPacketNumber == slot.packetNum) return;
        slot.packetNum = xi.dwPacketNumber;

        ControllerState newState = BuildState(static_cast<ControllerId>(index), xi);

        // Compute edge bits
        const uint32_t prevBtns = static_cast<uint32_t>(slot.state.buttons);
        const uint32_t currBtns = static_cast<uint32_t>(newState.buttons);
        newState.buttonsDown = static_cast<Button>(currBtns & ~prevBtns);
        newState.buttonsUp   = static_cast<Button>(prevBtns & ~currBtns);

        // Apply deadzone
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
// Callback registry
// ---------------------------------------------------------------------------

CallbackHandle XInputBackend::OnInput(InputEventCallback cb) {
    std::unique_lock lock(m_callbackMutex);
    const size_t idx = m_inputCallbacks.size();
    m_inputCallbacks.push_back(std::move(cb));
    return CallbackHandle([this, idx] {
        std::unique_lock lk(m_callbackMutex);
        if (idx < m_inputCallbacks.size()) m_inputCallbacks[idx] = nullptr;
    });
}

CallbackHandle XInputBackend::OnConnection(ConnectionCallback cb) {
    std::unique_lock lock(m_callbackMutex);
    const size_t idx = m_connCallbacks.size();
    m_connCallbacks.push_back(std::move(cb));
    return CallbackHandle([this, idx] {
        std::unique_lock lk(m_callbackMutex);
        if (idx < m_connCallbacks.size()) m_connCallbacks[idx] = nullptr;
    });
}

void XInputBackend::FireInput(const ControllerState& state) {
    std::shared_lock lock(m_callbackMutex);
    for (auto& cb : m_inputCallbacks) {
        if (cb) cb(state);
    }
}

void XInputBackend::FireConnection(ControllerId id, ConnectionEvent ev) {
    std::shared_lock lock(m_callbackMutex);
    for (auto& cb : m_connCallbacks) {
        if (cb) cb(id, ev);
    }
}

// ---------------------------------------------------------------------------
// Haptics
// ---------------------------------------------------------------------------

void XInputBackend::Rumble(ControllerId id, RumbleParams params) {
    if (id >= kMaxControllers) return;

    XINPUT_VIBRATION vib{};
    vib.wLeftMotorSpeed  = static_cast<WORD>(params.lowFreqMotor  * 65535.0f);
    vib.wRightMotorSpeed = static_cast<WORD>(params.highFreqMotor * 65535.0f);
    XInputSetState(id, &vib);

    if (params.durationMs > 0) {
        // Fire-and-forget stop on a detached thread
        const DWORD dur = params.durationMs;
        std::thread([id, dur] {
            Sleep(dur);
            XINPUT_VIBRATION stop{};
            XInputSetState(id, &stop);
        }).detach();
    }
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

std::vector<ControllerInfo> XInputBackend::GetConnectedControllers() const {
    std::shared_lock lock(m_stateMutex);
    std::vector<ControllerInfo> result;
    result.reserve(kMaxControllers);
    for (uint32_t i = 0; i < kMaxControllers; ++i) {
        if (m_slots[i].connected) {
            result.push_back({
                static_cast<ControllerId>(i),
                ControllerType::Xbox,
                "Xbox Controller #" + std::to_string(i)
            });
        }
    }
    return result;
}

ControllerState XInputBackend::GetState(ControllerId id) const {
    if (id >= kMaxControllers) return {};
    std::shared_lock lock(m_stateMutex);
    return m_slots[id].state;
}

} // namespace enjoystick::core
