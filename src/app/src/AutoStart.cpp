#include "AutoStart.hpp"

#include <Windows.h>
#include <array>

namespace enjoystick::app {

static std::wstring GetExePath() {
    std::array<wchar_t, MAX_PATH> buf{};
    GetModuleFileNameW(nullptr, buf.data(), MAX_PATH);
    return buf.data();
}

bool AutoStart::Enable() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &hKey)
        != ERROR_SUCCESS) return false;

    const std::wstring cmd = L'"' + GetExePath() + L"\" --autostart";
    const DWORD size = static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t));
    const bool ok = RegSetValueExW(
        hKey, kValueName, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(cmd.c_str()), size) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    return ok;
}

bool AutoStart::Disable() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &hKey)
        != ERROR_SUCCESS) return false;

    const bool ok = RegDeleteValueW(hKey, kValueName) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    return ok;
}

bool AutoStart::IsEnabled() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &hKey)
        != ERROR_SUCCESS) return false;

    const LSTATUS status = RegQueryValueExW(hKey, kValueName, nullptr,
                                             nullptr, nullptr, nullptr);
    RegCloseKey(hKey);
    return status == ERROR_SUCCESS;
}

} // namespace enjoystick::app
