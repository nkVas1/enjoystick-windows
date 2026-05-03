#include <enjoystick/app/AutoStart.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace enjoystick::app {

static constexpr wchar_t kRunKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

bool AutoStart::Enable(const std::filesystem::path& exePath) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return false;

    const std::wstring value = L"\"" + exePath.wstring() + L"\" --tray";
    const DWORD size = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LSTATUS r  = RegSetValueExW(
        hKey, kValueName, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value.c_str()), size);
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}

bool AutoStart::Disable() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return false;
    const LSTATUS r = RegDeleteValueW(hKey, kValueName);
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}

bool AutoStart::IsEnabled(const std::filesystem::path& exePath) {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &hKey) != ERROR_SUCCESS)
        return false;

    wchar_t buf[MAX_PATH * 2] = {};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    const LSTATUS r = RegQueryValueExW(hKey, kValueName, nullptr, &type,
                                        reinterpret_cast<BYTE*>(buf), &size);
    RegCloseKey(hKey);
    if (r != ERROR_SUCCESS) return false;
    return std::wstring(buf).find(exePath.wstring()) != std::wstring::npos;
}

} // namespace enjoystick::app
