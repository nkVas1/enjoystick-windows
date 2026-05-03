#pragma once

#include <enjoystick/shared/Types.hpp>
#include <cstdint>
#include <unordered_map>

namespace enjoystick::app {

///
/// KeyboardMapper — translates controller buttons to Win32 virtual-key presses.
///
/// This allows navigating any standard Windows dialog (file pickers, UAC prompts,
/// browser UI, settings panels) purely with a gamepad.
///
/// Mapping is editable at runtime via ConfigStore / UI settings screen.
///
struct ButtonMapping {
    WORD   virtualKey = 0;        ///< Win32 VK_ code
    bool   withShift  = false;    ///< Also hold Shift
    bool   withCtrl   = false;    ///< Also hold Ctrl
    bool   withAlt    = false;    ///< Also hold Alt
};

class KeyboardMapper {
public:
    KeyboardMapper();

    /// Process a controller state and fire key events for changed buttons.
    void Update(const ControllerState& state);

    /// Override a mapping at runtime.
    void SetMapping(Button btn, ButtonMapping mapping);

    /// Restore factory defaults.
    void ResetDefaults();

    /// Enable/disable the entire mapper (disabled when cursor mode is active).
    void SetEnabled(bool enabled) noexcept { m_enabled = enabled; }
    [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }

private:
    void PressKey(const ButtonMapping& m) const;
    void ReleaseKey(const ButtonMapping& m) const;

    std::unordered_map<uint32_t, ButtonMapping> m_map;
    Button m_prevButtons = Button::None;
    bool   m_enabled     = true;
};

} // namespace enjoystick::app
