#pragma once

// WIN32_LEAN_AND_MEAN / NOMINMAX injected by CMake target_compile_definitions.
// Do NOT redeclare them here to avoid C4005 in unity builds.
#include <Windows.h>
#include <Xinput.h>

#include <enjoystick/core/InputEngine.hpp>
#include <enjoystick/core/DeadzoneFilter.hpp>

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <array>
#include <vector>
#include <cstdint>
#include <string>

namespace enjoystick::core {

// Signature of XInputGetStateEx (ordinal 100) — identical to XInputGetState
// but also reports the Guide button in wButtons bit 0x0400.
using PFN_XInputGetStateEx = DWORD(WINAPI*)(DWORD dwUserIndex, XINPUT_STATE* pState);

///
/// XInputBackend (concrete implementation of InputEngine)
///
/// Polls up to XUSER_MAX_COUNT (4) XInput controllers at a configurable
/// rate (default 250 Hz = 4 ms intervals) on a dedicated high-priority thread.
///
/// Thread safety:
///   - Start()/Stop() are idempotent and safe to call from any thread.
///   - OnInput()/OnConnection() are safe to call from any thread.
///   - Callbacks are dispatched under a shared_mutex read-lock so multiple
///     callbacks execute concurrently; removing a callback (CallbackHandle
///     destructor) takes the write-lock and nulls the slot.
///   - GetState()/GetConnectedControllers() take the shared read-lock.
///
class XInputBackend final : public InputEngine {
public:
    explicit XInputBackend(Config cfg, DeadzoneFilter filter);
    ~XInputBackend() override;

    // InputEngine interface
    void Start() override;
    void Stop()  override;

    [[nodiscard]] CallbackHandle OnInput     (InputEventCallback  cb) override;
    [[nodiscard]] CallbackHandle OnConnection(ConnectionCallback  cb) override;

    void Rumble(ControllerId id, RumbleParams params) override;

    [[nodiscard]] std::vector<ControllerInfo> GetConnectedControllers() const override;
    [[nodiscard]] ControllerState             GetState(ControllerId id) const override;

private:
    // ---- XInput DLL / GetStateEx loading -----------------------------------
    void   LoadXInputDll();
    void   UnloadXInputDll();
    DWORD  DoGetState(uint32_t index, XINPUT_STATE* pState) const;

    void PollLoop();
    void PollController(uint32_t index);

    void FireInput     (const ControllerState& state);
    void FireConnection(ControllerId id, ConnectionEvent ev);

    // ---- XInput dynamic load -----------------------------------------------
    HMODULE               m_xinputModule  = nullptr;
    PFN_XInputGetStateEx  m_getStateEx    = nullptr;  ///< nullptr => use XInputGetState

    // ---- Config / filter ------------------------------------------------
    Config         m_config;
    DeadzoneFilter m_deadzone;

    // ---- Per-controller slot --------------------------------------------
    struct Slot {
        bool            connected  = false;
        DWORD           packetNum  = 0xFFFFFFFF;  // sentinel: force first-frame update
        ControllerState state      = {};
    };
    std::array<Slot, XUSER_MAX_COUNT> m_slots = {};
    mutable std::shared_mutex         m_stateMutex;

    // ---- Callback registry ----------------------------------------------
    // Slots are nulled on unregister rather than erased to preserve index
    // stability during concurrent iteration.
    struct InputCB   { uint64_t id; InputEventCallback  fn; };
    struct ConnCB    { uint64_t id; ConnectionCallback  fn; };

    mutable std::shared_mutex        m_callbackMutex;
    uint64_t                         m_nextCallbackId = 0;
    std::vector<InputCB>             m_inputCallbacks;
    std::vector<ConnCB>              m_connCallbacks;

    // ---- Poll thread ----------------------------------------------------
    std::thread        m_pollThread;
    std::atomic<bool>  m_running{false};
};

} // namespace enjoystick::core
