#pragma once
// =============================================================================
// OverlayController
//
// High-level controller that owns the application-level logic for the
// EnjoyStick overlay:  radial menu → keyboard / settings trigger, and
// clean lifetime management of all overlay sub-systems.
//
// Typical usage:
//   auto wnd = OverlayWindow::Create(cfg);
//   OverlayController ctrl(wnd.get());
//   ctrl.SetupDefaultRadialItems();      // or supply custom items
//   wnd->Show();
//
//   // On each input frame:
//   ctrl.PostState(state);
// =============================================================================

#include <enjoystick/overlay/OverlayWindow.hpp>
#include <enjoystick/overlay/RadialMenu.hpp>
#include <enjoystick/overlay/VirtualKeyboard.hpp>
#include <enjoystick/overlay/SettingsMenu.hpp>

#include <functional>
#include <string>

namespace enjoystick::overlay {

// Which widget currently owns controller focus.
enum class FocusOwner { None, RadialMenu, Keyboard, Settings };

class OverlayController {
public:
    using OnTextSubmitted  = std::function<void(const std::wstring&)>;
    using OnSettingsChanged = std::function<void(const SettingsMenu::Values&)>;

    explicit OverlayController(OverlayWindow* window);
    ~OverlayController() = default;

    // ---- Radial items -------------------------------------------------------
    // Replaces the current set of radial items.  Items with id=="keyboard"
    // or id=="settings" are automatically wired to open those panels.
    void SetRadialItems(std::vector<RadialMenuItem> items);

    // Convenience: populate a sensible default set of radial items.
    void SetupDefaultRadialItems();

    // ---- Callbacks ----------------------------------------------------------
    void SetOnTextSubmitted  (OnTextSubmitted   cb) { m_onText     = std::move(cb); }
    void SetOnSettingsChanged(OnSettingsChanged cb) { m_onSettings = std::move(cb); }

    // ---- Input forwarding ---------------------------------------------------
    // Call once per input frame from your input thread.
    void PostState(const ControllerState& state);

    // ---- Accessors ----------------------------------------------------------
    [[nodiscard]] FocusOwner    GetFocusOwner() const noexcept { return m_focus; }
    [[nodiscard]] OverlayWindow* Window()       const noexcept { return m_window; }

private:
    void WireRadialItemActions(std::vector<RadialMenuItem>& items);
    void OpenKeyboard();
    void OpenSettings();

    OverlayWindow*     m_window  = nullptr;
    FocusOwner         m_focus   = FocusOwner::None;
    OnTextSubmitted    m_onText;
    OnSettingsChanged  m_onSettings;
};

} // namespace enjoystick::overlay
