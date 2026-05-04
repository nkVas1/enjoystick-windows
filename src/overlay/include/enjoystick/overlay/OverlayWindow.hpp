#pragma once

#include <enjoystick/shared/Types.hpp>
#include <enjoystick/overlay/RadialMenu.hpp>
#include <enjoystick/overlay/SettingsMenu.hpp>
#include <memory>
#include <string>
#include <functional>
#include <cstdint>

// Forward declarations to avoid pulling Windows headers into user code
struct HWND__;
struct ID2D1Factory;
struct IDWriteFactory;

namespace enjoystick::overlay {

///
/// OverlayWindow — a per-monitor layered window rendered with Direct2D.
///
/// Architecture:
///   - HWND created with WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST
///   - DirectComposition for per-pixel alpha without GDI overhead
///   - Dedicated render thread; PostState() is called from the input thread
///     via a lock-free state snapshot.
///   - Supports multi-monitor setups: one OverlayWindow per HMONITOR
///
/// Usage:
///   auto overlay = OverlayWindow::Create();
///   overlay->Show();
///   // from input callback:
///   overlay->PostState(state);
///   // update the HUD mode chip:
///   overlay->SetModeLabel(L"\U0001F5B1  Cursor mode");
///
class OverlayWindow {
public:
    struct Config {
        /// Monitor index (0 = primary).
        uint32_t monitorIndex = 0;

        /// Target render rate in Hz (independent of input polling).
        uint32_t renderHz = 60;

        /// Show controller connection/disconnection toast notifications.
        bool showConnectionToasts = true;

        /// Duration a toast notification is visible (ms).
        uint32_t toastDurationMs = 2500;

        /// Show a small HUD indicator in the corner when a controller is active.
        bool showActiveIndicator = true;
    };

    static std::unique_ptr<OverlayWindow> Create(Config config = {});

    virtual ~OverlayWindow() = default;

    /// Start the render thread and show the window.
    virtual void Show() = 0;

    /// Hide the overlay and stop rendering.
    virtual void Hide() = 0;

    /// Post a new controller state (thread-safe, lock-free).
    virtual void PostState(const ControllerState& state) = 0;

    /// Access the radial quick-action menu.
    virtual RadialMenu& GetRadialMenu() = 0;

    /// Access the gamepad-driven settings panel.
    virtual SettingsMenu& GetSettingsMenu() = 0;

    /// Trigger a toast notification (thread-safe).
    virtual void ShowToast(std::wstring message, uint32_t durationMs = 2500) = 0;

    /// Update the HUD mode-chip label shown in the bottom-left corner.
    /// Pass an empty string to hide the chip. Thread-safe.
    virtual void SetModeLabel(std::wstring label) = 0;

    /// True if the overlay window is currently shown.
    [[nodiscard]] virtual bool IsShown() const noexcept = 0;

    /// Returns the native HWND (cast from HWND__*).
    [[nodiscard]] virtual HWND__* GetHWND() const noexcept = 0;
};

} // namespace enjoystick::overlay
