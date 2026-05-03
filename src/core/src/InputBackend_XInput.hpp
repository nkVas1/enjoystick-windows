#pragma once

// NOTE: WIN32_LEAN_AND_MEAN and NOMINMAX are injected by CMake
// for this module. Do NOT redefine them here to avoid C4005.
#include <Windows.h>
#include <Xinput.h>

#include <enjoystick/core/InputEngine.hpp>
#include <enjoystick/core/DeadzoneFilter.hpp>
#include <enjoystick/shared/Types.hpp>

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <array>
#include <vector>
#include <cstdint>

namespace enjoystick::core {

///
/// InputBackend_XInput
///
/// Polls up to 4 XInput controllers at a configurable rate (default 250 Hz).
/// Runs on its own high-priority thread and dispatches InputEventCallback +
/// ConnectionCallback to registered listeners under a shared mutex.
///
/// Design decisions:
///   - One thread, four controllers polled sequentially each tick.
///     At 250 Hz the total XInput overhead is < 0.1 ms per tick.
///   - ButtonsDown / ButtonsUp edge tracking per controller.
///   - DeadzoneFilter applied after raw axis reads.
///   - Rumble dispatched synchronously on the polling thread to minimise
///     latency between the request and the vibration motor.
///
class InputBackend_XInput final {
public:
    struct Config {
        uint32_t pollingRateHz  = 250;
        bool     hapticsEnabled = true;
        bool     verboseLogging = false;
        InputEngine::Config engineConfig;
    };

    explicit InputBackend_XInput(Config cfg, DeadzoneFilter filter);
    ~InputBackend_XInput();

    void Start();
    void Stop();

    // Listener registration (thread-safe)
    CallbackHandle AddInputListener(InputEventCallback cb);
    CallbackHandle AddConnectionListener(ConnectionCallback cb);

    void Rumble(ControllerId id, RumbleParams params);
    [[nodiscard]] std::vector<ControllerInfo>    GetConnectedControllers() const;
    [[nodiscard]] ControllerState                GetState(ControllerId id) const;

private:
    void PollLoop();
    void ProcessController(ControllerId id, const XINPUT_STATE& raw);
    void NotifyDisconnected(ControllerId id);

    // Convert raw XInput axes to normalised [-1, 1] float
    static float NormaliseAxis(SHORT value, SHORT deadzone);
    static float NormaliseTrigger(BYTE value);

    Config m_cfg;
    DeadzoneFilter m_deadzone;

    struct ControllerSlot {
        bool          connected = false;
        DWORD         packetNumber = 0;
        ControllerState lastState = {};
    };
    std::array<ControllerSlot, XUSER_MAX_COUNT> m_slots;

    mutable std::mutex m_listenerMutex;
    uint64_t           m_nextListenerId = 0;
    std::vector<std::pair<uint64_t, InputEventCallback>>      m_inputListeners;
    std::vector<std::pair<uint64_t, ConnectionCallback>>      m_connListeners;

    std::thread       m_pollThread;
    std::atomic<bool> m_running{false};
};

} // namespace enjoystick::core
