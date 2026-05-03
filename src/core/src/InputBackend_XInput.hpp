#pragma once

#include <enjoystick/core/InputEngine.hpp>
#include <enjoystick/core/DeadzoneFilter.hpp>
#include <enjoystick/core/HapticsEngine.hpp>
#include <Windows.h>
#include <Xinput.h>
#include <array>
#include <atomic>
#include <mutex>
#include <thread>

namespace enjoystick::core {

class XInputBackend {
public:
    static constexpr uint32_t kMaxControllers = XUSER_MAX_COUNT; // 4

    struct ControllerSlot {
        bool          connected  = false;
        DWORD         packetNum  = 0xFFFFFFFF;
        ControllerState state    = {};
    };

    explicit XInputBackend(const InputEngine::Config& cfg, DeadzoneFilter dz);
    ~XInputBackend();

    void Start();
    void Stop();

    [[nodiscard]] CallbackHandle OnInput(InputEventCallback cb);
    [[nodiscard]] CallbackHandle OnConnection(ConnectionCallback cb);

    void Rumble(ControllerId id, RumbleParams params);
    [[nodiscard]] std::vector<ControllerInfo> GetConnectedControllers() const;
    [[nodiscard]] ControllerState GetState(ControllerId id) const;

private:
    void PollLoop();
    void PollController(uint32_t index);

    InputEngine::Config     m_config;
    DeadzoneFilter          m_deadzone;
    std::array<ControllerSlot, kMaxControllers> m_slots;

    std::atomic<bool>       m_running{false};
    std::thread             m_pollThread;
    mutable std::mutex      m_stateMutex;

    std::vector<InputEventCallback>     m_inputCallbacks;
    std::vector<ConnectionCallback>     m_connCallbacks;
    mutable std::mutex                  m_callbackMutex;
};

} // namespace enjoystick::core
