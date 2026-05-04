#pragma once

// WIN32_LEAN_AND_MEAN and NOMINMAX are injected by CMake target_compile_definitions.
#include <Windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <enjoystick/overlay/OverlayWindow.hpp>

#include <atomic>
#include <mutex>
#include <thread>
#include <queue>
#include <string>
#include <vector>

namespace enjoystick::overlay {

struct ToastNotification {
    std::wstring message;
    uint32_t     durationMs;
    float        elapsed = 0.0f;  ///< milliseconds elapsed since shown
};

///
/// OverlayWindowImpl — layered click-through window with per-pixel alpha.
///
/// Render pipeline:
///   CreateCompatibleDC + 32-bpp DIB
///   → ID2D1DCRenderTarget (cached; BindDC only on size change)
///   → BeginDraw / ... / EndDraw
///   → UpdateLayeredWindow(ULW_ALPHA)   ← true per-pixel transparency
///
/// Threading model:
///   Show()       — signals m_startEvent; returns immediately.
///   Render thread — creates HWND + D2D, runs frame loop, pumps its own
///                   WndProc messages, destroys everything on exit.
///   Hide()       — posts WM_QUIT to render thread; waits for join.
///   PostState()  — lock-free ping-pong buffer (any thread).
///   ShowToast()  — mutex-protected queue (any thread).
///
class OverlayWindowImpl final : public OverlayWindow {
public:
    explicit OverlayWindowImpl(Config config);
    ~OverlayWindowImpl() override;

    void Show()  override;
    void Hide()  override;
    void PostState(const ControllerState& state) override;
    RadialMenu&    GetRadialMenu()    override;
    SettingsMenu&  GetSettingsMenu()  override;
    void ShowToast(std::wstring message, uint32_t durationMs) override;
    [[nodiscard]] bool    IsShown()  const noexcept override;
    [[nodiscard]] HWND__* GetHWND()  const noexcept override;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    // Render-thread entry point and helpers
    void RenderThread();
    void CreateWindowAndD2D();
    void DestroyWindowAndD2D();
    void EnsureDib(int w, int h);
    void DestroyDib();
    bool EnsureRenderTarget();  ///< (re)creates / binds RT when DIB changed
    void PumpMessages();        ///< non-blocking WndProc dispatch
    void RenderFrame(float deltaSeconds);

    void DrawActiveIndicator(ID2D1RenderTarget* rt);
    void DrawHudMode(ID2D1RenderTarget* rt);         ///< NEW: mode chip bottom-left
    void DrawToasts(ID2D1RenderTarget* rt, float deltaSeconds);

    Config   m_config;
    HWND     m_hwnd    = nullptr;
    HMONITOR m_monitor = nullptr;
    Rect     m_monitorRect = {};

    // D2D / DirectWrite
    Microsoft::WRL::ComPtr<ID2D1Factory1>       m_d2dFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory>      m_dwriteFactory;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> m_renderTarget;  ///< CACHED

    // DIB render surface (per-pixel alpha path)
    HDC     m_memDC  = nullptr;
    HBITMAP m_hDib   = nullptr;
    int     m_dibW   = 0;
    int     m_dibH   = 0;
    bool    m_dibDirty = true;  ///< true ⇒ re-bind RT to new DIB before next frame

    // State double-buffer (input thread → render thread, lock-free).
    // alignas(64) prevents false sharing with adjacent members.
#pragma warning(push)
#pragma warning(disable: 4324)
    alignas(64) std::atomic<ControllerState*> m_pendingState{nullptr};
#pragma warning(pop)
    ControllerState m_stateBuffers[2];
    int             m_activeBuffer = 0;
    ControllerState m_lastState    = {};

    // Current mode label (set from application thread via SetModeLabel)
    mutable std::mutex m_modeLabelMutex;
    std::wstring       m_modeLabel;

    // Overlay components
    RadialMenu    m_radialMenu;
    SettingsMenu  m_settingsMenu;

    // Toast queue
    std::mutex                      m_toastMutex;
    std::queue<ToastNotification>   m_pendingToasts;
    std::vector<ToastNotification>  m_activeToasts;

    // Render thread lifecycle
    std::thread        m_renderThread;
    std::atomic<bool>  m_running{false};
    std::atomic<bool>  m_shown{false};
    HANDLE             m_startEvent = nullptr;  ///< signalled by Show()

    float m_dpiScale = 1.0f;
};

} // namespace enjoystick::overlay
