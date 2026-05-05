#pragma once
// =============================================================================
// OverlayController
//
// High-level controller that owns the application-level logic for the
// EnjoyStick overlay:  radial menu → keyboard / settings / controls trigger,
// and clean lifetime management of all overlay sub-systems.
//
// Typical usage:
//   auto wnd = OverlayWindow::Create(cfg);
//   OverlayController ctrl(wnd.get());
//   ctrl.SetupDefaultRadialItems();      // or supply custom items
//   ctrl.SetOnSettingsChanged([](auto& v){ /* apply */ });
//   wnd->Show();
//
//   // On each input frame (from your input thread):
//   ctrl.PostState(state);
//
//   // When the active mode changes (Cursor / Navigate / …):
//   ctrl.SetModeLabel(L"\U0001F5B1  Cursor mode");
// =============================================================================

#include <enjoystick/overlay/OverlayWindow.hpp>
#include <enjoystick/overlay/RadialMenu.hpp>
#include <enjoystick/overlay/VirtualKeyboard.hpp>
#include <enjoystick/overlay/SettingsMenu.hpp>

#include <functional>
#include <string>

namespace enjoystick::overlay {

// Which widget currently owns controller focus.
enum class FocusOwner { None, RadialMenu, Keyboard, Settings, Controls };

class OverlayController {
public:
    using OnTextSubmitted   = std::function<void(const std::wstring&)>;
    using OnSettingsChanged = std::function<void(const SettingsMenu::Values&)>;

    explicit OverlayController(OverlayWindow* window);
    ~OverlayController() = default;

    // ---- Radial items -------------------------------------------------------
    // Replaces the current set of radial items.  Items with known ids
    // (keyboard / settings / controls / cut / copy / paste / undo) are
    // automatically wired to the appropriate action.
    void SetRadialItems(std::vector<RadialMenuItem> items);

    // Convenience: populate a sensible default set of radial items.
    void SetupDefaultRadialItems();

    // ---- Callbacks ----------------------------------------------------------
    void SetOnTextSubmitted  (OnTextSubmitted   cb) { m_onText     = std::move(cb); }
    void SetOnSettingsChanged(OnSettingsChanged cb) { m_onSettings = std::move(cb); }

    // ---- Settings -----------------------------------------------------------
    // Push the current settings values so that the next OpenSettings() call
    // will pre-populate the panel with real values rather than defaults.
    void SetCurrentSettings(const SettingsMenu::Values& v) { m_currentSettings = v; }
    [[nodiscard]] const SettingsMenu::Values& GetCurrentSettings() const noexcept
        { return m_currentSettings; }

    // ---- HUD mode label -----------------------------------------------------
    // Sets the bottom-left HUD chip text.  Pass an empty string to hide.
    // Thread-safe (delegates to OverlayWindow::SetModeLabel).
    void SetModeLabel(std::wstring label);

    // ---- Input forwarding ---------------------------------------------------
    // Call once per input frame from your input thread.
    void PostState(const ControllerState& state);

    // ---- Accessors ----------------------------------------------------------
    [[nodiscard]] FocusOwner     GetFocusOwner()  const noexcept { return m_focus; }
    [[nodiscard]] OverlayWindow* Window()         const noexcept { return m_window; }

private:
    void WireRadialItemActions(std::vector<RadialMenuItem>& items);
    void OpenKeyboard();
    void OpenSettings();
    void OpenControls();

    OverlayWindow*       m_window          = nullptr;
    FocusOwner           m_focus           = FocusOwner::None;
    SettingsMenu::Values m_currentSettings;
    std::wstring         m_pendingModeLabel;

    OnTextSubmitted    m_onText;
    OnSettingsChanged  m_onSettings;
};

} // namespace enjoystick::overlay
