#pragma once

#include <enjoystick/shared/Types.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace enjoystick::input {

///
/// KeyboardMapper — translates controller button events into keyboard
/// and mouse-button SendInput calls so that *any* Windows application
/// receives standard keyboard messages without special controller support.
///
/// Default bindings (Steam-Deck-inspired UI navigation layer):
///   D-pad Up/Down/Left/Right  →  Arrow keys
///   South (A/Cross)           →  Enter
///   East  (B/Circle)          →  Escape
///   North (Y/Triangle)        →  F5  (refresh / context action)
///   West  (X/Square)          →  Space
///   LB                        →  Tab
///   RB                        →  Shift+Tab  (reverse focus)
///   Start                     →  Escape
///   Select                    →  Windows key
///   LS click                  →  F2  (rename)
///   RS click                  →  Application key (context menu)
///
/// All bindings are runtime-configurable; custom profiles can be
/// loaded from ConfigStore.
///

struct KeyBinding {
    uint16_t vkCode    = 0;      ///< Virtual key code (VK_*)
    bool     withShift = false;  ///< Inject Shift modifier
    bool     withCtrl  = false;  ///< Inject Ctrl modifier
    bool     withAlt   = false;  ///< Inject Alt modifier
    bool     extended  = false;  ///< KEYEVENTF_EXTENDEDKEY flag
};

class KeyboardMapper {
public:
    KeyboardMapper();

    /// Bind a button to a key sequence. Pass an empty binding to unbind.
    void Bind(Button button, KeyBinding binding);
    void Unbind(Button button);

    /// Clear all bindings and restore defaults.
    void ResetToDefaults();

    /// Call every frame with the latest controller state.
    /// Synthesises key-down on button press, key-up on release.
    void Update(const ControllerState& state);

    /// Temporarily disable all key injection (e.g. while in cursor mode).
    void SetEnabled(bool enabled) noexcept;
    [[nodiscard]] bool IsEnabled() const noexcept;

private:
    void InjectKey(const KeyBinding& binding, bool down);
    void SendModifiers(const KeyBinding& binding, bool down);

    std::unordered_map<uint32_t, KeyBinding> m_bindings;  ///< Button mask → binding
    Button  m_prevButtons = Button::None;
    bool    m_enabled     = true;
};

} // namespace enjoystick::input
