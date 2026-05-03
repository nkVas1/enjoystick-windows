#pragma once

#include <string>
#include <filesystem>

namespace enjoystick::app {

///
/// AutoStart — manages Windows Registry autostart entry for Enjoystick.
///
/// Writes to HKCU\Software\Microsoft\Windows\CurrentVersion\Run
/// so the application launches at login without UAC elevation.
///
class AutoStart {
public:
    static constexpr wchar_t kValueName[] = L"EnjoyStickWindows";

    /// Register the given executable path for autostart.
    [[nodiscard]] static bool Enable(const std::filesystem::path& exePath);

    /// Remove the autostart entry.
    [[nodiscard]] static bool Disable();

    /// Check if the entry currently exists and points to exePath.
    [[nodiscard]] static bool IsEnabled(const std::filesystem::path& exePath);
};

} // namespace enjoystick::app
