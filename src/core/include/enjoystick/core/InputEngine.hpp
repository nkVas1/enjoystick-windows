#pragma once

#include <enjoystick/shared/Types.hpp>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace enjoystick::core {

/// Callback invoked on every controller state update (on the polling thread).
using InputEventCallback = std::function<void(const ControllerState&)>;

/// Callback invoked when a controller connects or disconnects.
using ConnectionCallback = std::function<void(ControllerId, ConnectionEvent)>;

///
/// InputEngine — high-level interface to the controller input subsystem.
///
/// Thread safety: all public methods are thread-safe.
/// Callbacks are dispatched on an internal high-priority thread; keep them short.
///
class InputEngine {
public:
    struct Config {
        /// Target polling rate in Hz. Default: 250 Hz for sub-4ms latency.
        uint32_t pollingRateHz = 250;

        /// Which backend to prefer. Falls back to the other if unavailable.
        InputBackendPreference backendPreference = InputBackendPreference::Auto;

        /// Enable haptic feedback support.
        bool hapticsEnabled = true;

        /// Log verbose input events (disable in release).
        bool verboseLogging = false;
    };

    static std::unique_ptr<InputEngine> Create(Config config = {});

    virtual ~InputEngine() = default;

    /// Start the polling loop. Must be called before any events are received.
    virtual void Start() = 0;

    /// Gracefully stop the polling loop and release hardware resources.
    virtual void Stop() = 0;

    /// Register a callback for controller input events.
    /// Returns a handle that unregisters the callback when destroyed.
    [[nodiscard]] virtual CallbackHandle OnInput(InputEventCallback callback) = 0;

    /// Register a callback for connection/disconnection events.
    [[nodiscard]] virtual CallbackHandle OnConnection(ConnectionCallback callback) = 0;

    /// Send a haptic pulse to a specific controller.
    virtual void Rumble(ControllerId id, RumbleParams params) = 0;

    /// Enumerate currently connected controllers.
    virtual std::vector<ControllerInfo> GetConnectedControllers() const = 0;

    /// Get the latest polled state for a controller (zero-copy snapshot).
    virtual ControllerState GetState(ControllerId id) const = 0;
};

} // namespace enjoystick::core
