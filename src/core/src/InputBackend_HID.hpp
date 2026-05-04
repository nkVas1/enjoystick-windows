#pragma once

// WIN32_LEAN_AND_MEAN / NOMINMAX injected by CMake.
#include <Windows.h>
#include <hidsdi.h>
#include <setupapi.h>

#include <enjoystick/core/InputEngine.hpp>
#include <enjoystick/core/DeadzoneFilter.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

namespace enjoystick::core {

///
/// HidBackend — DualSense (Sony VID 0x054C, PID 0x0CE6 / 0x0DF2) input
/// backend using Win32 HID API.  Shares the same InputEngine interface as
/// XInputBackend so InputEngine::Create can select between them.
///
/// Design notes:
///   * USB Input Report 0x01 is 64 bytes.  BT is 78 bytes (report 0x31).
///     Bluetooth is detected via HIDP_CAPS and handled transparently.
///   * ReadFile is called in overlapped mode on a dedicated high-priority thread.
///   * Only the first DualSense found is used (index 0).
///   * After a disconnect the poll thread enters a wait-then-enumerate loop
///     (kReconnectIntervalMs) until the device reappears or Stop() is called.
///
class HidBackend final : public InputEngine {
public:
    explicit HidBackend(Config cfg, DeadzoneFilter filter);
    ~HidBackend() override;

    /// Returns true if at least one DualSense device was found and opened.
    [[nodiscard]] bool TryOpen();

    // InputEngine interface
    void Start() override;
    void Stop()  override;

    [[nodiscard]] CallbackHandle OnInput     (InputEventCallback cb) override;
    [[nodiscard]] CallbackHandle OnConnection(ConnectionCallback cb) override;

    void Rumble(ControllerId id, RumbleParams params) override;

    [[nodiscard]] std::vector<ControllerInfo> GetConnectedControllers() const override;
    [[nodiscard]] ControllerState             GetState(ControllerId id) const override;

private:
    void PollLoop();
    void ReconnectLoop();
    bool OpenDevice();   ///< Enumerate HID buses and open the first DualSense found.
    bool ParseReport(const uint8_t* buf, size_t len, ControllerState& out) const noexcept;
    void SendRumbleReport(float low, float high) noexcept;
    void CloseDevice() noexcept;

    void FireInput     (const ControllerState& s);
    void FireConnection(ControllerId id, ConnectionEvent ev);

    // ---- Device handle ---------------------------------------------------
    HANDLE  m_device   = INVALID_HANDLE_VALUE;
    bool    m_isBt     = false;   ///< true = Bluetooth (78-byte report)
    std::string m_devicePath;

    // ---- Config / filter ------------------------------------------------
    Config         m_config;
    DeadzoneFilter m_deadzone;

    // ---- State ----------------------------------------------------------
    mutable std::shared_mutex  m_stateMutex;
    ControllerState            m_state     = {};
    bool                       m_connected = false;
    Vec2                       m_prevTouchpad = {};
    bool                       m_prevTouchActive = false;

    // ---- Callbacks -------------------------------------------------------
    struct InputCB { uint64_t id; InputEventCallback  fn; };
    struct ConnCB  { uint64_t id; ConnectionCallback  fn; };
    mutable std::shared_mutex m_callbackMutex;
    uint64_t                  m_nextId = 0;
    std::vector<InputCB>      m_inputCbs;
    std::vector<ConnCB>       m_connCbs;

    // ---- Poll thread ----------------------------------------------------
    std::thread       m_thread;
    std::atomic<bool> m_running{false};

    // ---- Reconnect -------------------------------------------------------
    /// Interval between re-enumeration attempts after a disconnect (ms).
    static constexpr DWORD kReconnectIntervalMs = 1500;

    // DualSense USB vendor/product IDs
    static constexpr uint16_t kSonyVid      = 0x054C;
    static constexpr uint16_t kDsPid_USB    = 0x0CE6;
    static constexpr uint16_t kDsPid_Edge   = 0x0DF2;
};

} // namespace enjoystick::core
