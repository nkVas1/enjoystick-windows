#pragma once

#include <string>
#include <functional>
#include <memory>
#include <cstdint>
#include <vector>

namespace enjoystick::app {

///
/// SystemTray — a Shell_NotifyIcon wrapper with a right-click context menu.
///
/// Menu items are registered as (label, callback) pairs.
/// This class owns the HWND used for Shell notification messages.
///
struct TrayMenuItem {
    std::wstring             label;
    std::function<void()>    action;
    bool                     separator = false;  ///< Render as menu separator
};

class SystemTray {
public:
    static std::unique_ptr<SystemTray> Create(
        std::wstring tooltip,
        std::wstring iconResourcePath = L"");

    virtual ~SystemTray() = default;

    /// Add items to the right-click context menu.
    virtual void SetMenuItems(std::vector<TrayMenuItem> items) = 0;

    /// Update the tooltip shown on hover.
    virtual void SetTooltip(std::wstring tooltip) = 0;

    /// Show a balloon notification.
    virtual void ShowBalloon(
        std::wstring title,
        std::wstring text,
        uint32_t     durationMs = 3000) = 0;

    /// Register a callback invoked on left-button double-click of the tray icon.
    /// Passing nullptr clears a previously registered callback.
    virtual void SetOnDoubleClick(std::function<void()> callback) = 0;

    /// Remove the icon from the tray.
    virtual void Remove() = 0;
};

} // namespace enjoystick::app
