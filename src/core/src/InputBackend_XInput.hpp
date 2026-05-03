#pragma once

// NOMINMAX prevents Windows.h from defining min/max macros that
// conflict with std::min, std::max, std::clamp on MSVC.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <enjoystick/core/InputEngine.hpp>
#include <enjoystick/core/DeadzoneFilter.hpp>
#include <enjoystick/core/HapticsEngine.hpp>
#include <Windows.h>
#include <Xinput.h>
#include <array>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

namespace enjoystick::core {

class XInputBackend final : public InputEngine {
public:
    static constexpr uint32_t kMaxControllers = XUSER_MAX_COUNT; // 4

    struct ControllerSlot {
        bool            connected = false;
        DWORD           packetNum = 0xFFFF'FFFF;
        ControllerState state     = {};
    };

    explicit XInputBackend(const InputEngine::Config& cfg, DeadzoneFilter dz);
    ~XInputBackend() override;

    void Start()  override;
    void Stop()   override;

    [[nodiscard]] CallbackHandle OnInput(InputEventCallback cb)      override;
    [[nodiscard]] CallbackHandle OnConnection(ConnectionCallback cb) override;

    void Rumble(ControllerId id, RumbleParams params)                override;
    [[nodiscard]] std::vector<ControllerInfo> GetConnectedControllers() const override;
    [[nodiscard]] ControllerState GetState(ControllerId id)          const override;

private:
    void PollLoop();
    void PollController(uint32_t index);
    void FireInput(const ControllerState& state);
    void FireConnection(ControllerId id, ConnectionEvent ev);

    InputEngine::Config m_config;
    DeadzoneFilter      m_deadzone;

    std::array<ControllerSlot, kMaxControllers> m_slots;
    mutable std::shared_mutex m_stateMutex;

    std::atomic<bool> m_running{false};
    std::thread       m_pollThread;

    std::vector<InputEventCallback>  m_inputCallbacks;
    std::vector<ConnectionCallback>  m_connCallbacks;
    mutable std::shared_mutex        m_callbackMutex;
};

} // namespace enjoystick::core
