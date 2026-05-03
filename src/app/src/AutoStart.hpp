#pragma once

#include <string>

namespace enjoystick::app {

///
/// AutoStart — manages the Windows registry run key so Enjoystick
/// launches automatically on user login.
///
/// Key:  HKCU\Software\Microsoft\Windows\CurrentVersion\Run
/// Value: "EnjoyStick" = "<path_to_exe> --autostart"
///
/// No elevation required (HKCU).
///
class AutoStart {
public:
    /// Register the current executable in the run key.
    static bool Enable();

    /// Remove the run key entry.
    static bool Disable();

    /// Returns true if the run key entry currently exists.
    [[nodiscard]] static bool IsEnabled();

private:
    static constexpr const wchar_t* kValueName = L"EnjoyStick";
    static constexpr const wchar_t* kRunKey    =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
};

} // namespace enjoystick::app
