#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <Windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")

#include <enjoystick/input/GameModeDetector.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>

namespace enjoystick::input {

// ---------------------------------------------------------------------------
// Known game-engine window class names
// ---------------------------------------------------------------------------
static constexpr const wchar_t* kKnownGameClassTable[] = {
    L"UnrealWindow",
    L"SDL_app",
    L"LWJGL",
    L"SFML_Window",
    L"GodotEngineWindow",
    L"Direct3DWindowClass",
    L"DXGISwapChain",
    L"Valve001",           // Steam overlay host
    L"CryENGINE",
    L"NativeWindow",       // Unity on some platforms
    L"GLUT",
};

// ---------------------------------------------------------------------------
// Known game / launcher exe names (lower-case, no path)
// ---------------------------------------------------------------------------
static constexpr const wchar_t* kKnownGameExeTable[] = {
    L"steam.exe",
    L"epicgameslauncher.exe",
    L"gog galaxy.exe",
    L"origin.exe",
    L"eadesktop.exe",
    L"upc.exe",             // Ubisoft Connect
    L"battle.net.exe",
    L"minecraft.exe",
    L"javaw.exe",           // Minecraft Java
    L"csgo.exe",
    L"cs2.exe",
    L"dota2.exe",
    L"hl2.exe",
    L"witcher3.exe",
    L"cyberpunk2077.exe",
    L"rdr2.exe",
    L"eldenring.exe",
    L"fortnite.exe",
    L"valorant-win64-shipping.exe",
    L"destiny2.exe",
    L"overwatch.exe",
    L"r5apex.exe",          // Apex Legends
    L"starcraft2.exe",
    L"hearthstone.exe",
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::wstring ToLower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
    return s;
}

std::wstring GameModeDetector::GetExeNameForHwnd(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) return {};

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return {};

    wchar_t path[MAX_PATH] = {};
    DWORD   sz   = MAX_PATH;
    if (!QueryFullProcessImageNameW(hProc, 0, path, &sz)) {
        CloseHandle(hProc);
        return {};
    }
    CloseHandle(hProc);

    // Extract filename only
    const wchar_t* slash = wcsrchr(path, L'\\');
    return ToLower(slash ? (slash + 1) : path);
}

// ---------------------------------------------------------------------------
// Detection passes
// ---------------------------------------------------------------------------

bool GameModeDetector::CheckFullscreenExclusive(
    HWND hwnd, HMONITOR hmon) const noexcept
{
    if (!hwnd || !hmon) return false;

    // Must be a top-level, visible, non-layered window
    const LONG style   = GetWindowLongW(hwnd, GWL_STYLE);
    const LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (!(style & WS_VISIBLE))           return false;
    if (exStyle & WS_EX_LAYERED)         return false;
    if (exStyle & WS_EX_TOOLWINDOW)      return false;

    // Window client rect must cover the entire monitor work area
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hmon, &mi))     return false;

    RECT wr{};
    if (!GetWindowRect(hwnd, &wr))       return false;

    return wr.left   <= mi.rcMonitor.left
        && wr.top    <= mi.rcMonitor.top
        && wr.right  >= mi.rcMonitor.right
        && wr.bottom >= mi.rcMonitor.bottom;
}

bool GameModeDetector::CheckFullscreenBorderless(HWND hwnd) const noexcept {
    if (!hwnd) return false;

    const LONG style   = GetWindowLongW(hwnd, GWL_STYLE);
    const LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);

    // Borderless fullscreen: popup, no caption, no menu bar, not an overlay
    if (!(style  & WS_POPUP))           return false;
    if (  style  & WS_CAPTION)          return false;
    if (  exStyle & WS_EX_TOOLWINDOW)   return false;
    if (  exStyle & WS_EX_LAYERED)      return false;

    RECT wr{};
    if (!GetWindowRect(hwnd, &wr))      return false;

    // Must span the full virtual screen
    const int vsLeft   = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vsTop    = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vsRight  = vsLeft + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vsBottom = vsTop  + GetSystemMetrics(SM_CYVIRTUALSCREEN);

    // Allow ±1 px tolerance for per-monitor DPI rounding
    return wr.left  <= vsLeft  + 1
        && wr.top   <= vsTop   + 1
        && wr.right >= vsRight - 1
        && wr.bottom>= vsBottom- 1;
}

bool GameModeDetector::CheckKnownWindowClass(HWND hwnd) const noexcept {
    if (!hwnd) return false;
    wchar_t cls[256] = {};
    if (!GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)))) return false;

    for (const wchar_t* known : kKnownGameClassTable) {
        if (_wcsicmp(cls, known) == 0) return true;
    }
    return false;
}

bool GameModeDetector::CheckKnownExe(HWND hwnd) noexcept {
    m_activeExeName = GetExeNameForHwnd(hwnd);
    if (m_activeExeName.empty()) return false;

    for (const wchar_t* known : kKnownGameExeTable) {
        if (m_activeExeName == known) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Poll
// ---------------------------------------------------------------------------
bool GameModeDetector::Poll(uint32_t intervalMs) noexcept {
    const DWORD now = GetTickCount();
    if (m_lastCheckMs != 0 && (now - m_lastCheckMs) < intervalMs) {
        return m_gameActive;
    }
    m_lastCheckMs = now;

    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        m_gameActive = false;
        m_activeExeName.clear();
        return false;
    }

    HMONITOR hmon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

    // Try each heuristic in order of cheapness.
    // Any positive match is sufficient.
    if (CheckKnownWindowClass(hwnd)
     || CheckFullscreenBorderless(hwnd)
     || CheckFullscreenExclusive(hwnd, hmon)
     || CheckKnownExe(hwnd))
    {
        m_gameActive = true;
        // Ensure m_activeExeName is always populated on positive match
        if (m_activeExeName.empty())
            m_activeExeName = GetExeNameForHwnd(hwnd);
        return true;
    }

    m_activeExeName.clear();
    m_gameActive = false;
    return false;
}

} // namespace enjoystick::input
