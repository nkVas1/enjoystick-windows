#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <string>
#include <cstdint>

namespace enjoystick::input {

///
/// GameModeDetector
/// ================
/// Determines whether the user is currently running a game as the foreground
/// application.  Uses four complementary heuristics:
///
///   1. Fullscreen exclusive window (covers entire virtual desktop,
///      non-layered, non-overlay style bits)
///   2. Fullscreen borderless window (WS_POPUP, virtual desktop size,
///      no menu/caption)
///   3. Known game-engine window class names (UnrealWindow, SDL_app, etc.)
///   4. Known game/launcher executable names
///
/// Designed to be called every frame with Poll(); internally rate-limits
/// the heavyweight checks to once per kDefaultPollIntervalMs.
///
class GameModeDetector {
public:
    GameModeDetector() = default;
    ~GameModeDetector() = default;

    /// Poll for game mode, rate-limited to intervalMs.
    /// Returns the current IsGameActive() value.
    bool Poll(uint32_t intervalMs = kDefaultPollIntervalMs) noexcept;

    /// Returns the cached result from the last Poll().  Thread-safe read.
    [[nodiscard]] bool IsGameActive() const noexcept { return m_gameActive; }

    /// Returns the exe name of the foreground window (empty if unknown).
    /// Useful for debug overlays and user-facing tooltips.
    [[nodiscard]] const std::wstring& GetActiveExeName() const noexcept { return m_activeExeName; }

    // How often (in ms) the heavy win32 checks are re-run.
    static constexpr uint32_t kDefaultPollIntervalMs = 500;

private:
    bool   m_gameActive  = false;
    DWORD  m_lastCheckMs = 0;
    std::wstring m_activeExeName;

    // -----------------------------------------------------------------------
    // Detection passes
    // -----------------------------------------------------------------------
    [[nodiscard]] bool CheckFullscreenExclusive(HWND hwnd, HMONITOR hmon) const noexcept;
    [[nodiscard]] bool CheckFullscreenBorderless(HWND hwnd) const noexcept;
    [[nodiscard]] bool CheckKnownWindowClass(HWND hwnd) const noexcept;
    [[nodiscard]] bool CheckKnownExe(HWND hwnd) noexcept;

    // Internal
    static std::wstring GetExeNameForHwnd(HWND hwnd);
};

} // namespace enjoystick::input
