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
/// OverlayWindowImpl — layered window with per-pixel alpha.
///
/// Render pipeline:
///   CreateCompatibleDC + 32-bpp DIB
///   → ID2D1DCRenderTarget::BindDC(memDC)
///   → BeginDraw / ... / EndDraw
///   → UpdateLayeredWindow(ULW_ALPHA)   ← true per-pixel transparency
///
/// Background Clear(0,0,0,0) → genuinely invisible.
/// Only drawn primitives are visible on screen.
///
class OverlayWindowImpl final : public OverlayWindow {
public:
    explicit OverlayWindowImpl(Config config);
    ~OverlayWindowImpl() override;

    void Show()  override;
    void Hide()  override;
    void PostState(const ControllerState& state) override;
    RadialMenu& GetRadialMenu() override;
    void ShowToast(std::wstring message, uint32_t durationMs) override;
    [[nodiscard]] bool    IsShown()  const noexcept override;
    [[nodiscard]] HWND__* GetHWND()  const noexcept override;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void CreateWindowAndD2D();
    void DestroyWindowAndD2D();
    void EnsureDib(int w, int h);
    void DestroyDib();
    void RenderLoop();
    void RenderFrame(float deltaSeconds);

    void DrawActiveIndicator(ID2D1RenderTarget* rt);
    void DrawToasts(ID2D1RenderTarget* rt, float deltaSeconds);

    Config   m_config;
    HWND     m_hwnd    = nullptr;
    HMONITOR m_monitor = nullptr;
    Rect     m_monitorRect = {};

    // D2D / DirectWrite
    Microsoft::WRL::ComPtr<ID2D1Factory1>  m_d2dFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;

    // DIB render surface (per-pixel alpha path)
    HDC     m_memDC  = nullptr;
    HBITMAP m_hDib   = nullptr;
    int     m_dibW   = 0;
    int     m_dibH   = 0;

    // State double-buffer (input thread → render thread, lock-free).
    // alignas(64) is intentional: it places m_pendingState on its own
    // cache line, preventing false sharing. C4324 (padding warning) is
    // expected and benign — suppressed locally.
#pragma warning(push)
#pragma warning(disable: 4324)
    alignas(64) std::atomic<ControllerState*> m_pendingState{nullptr};
#pragma warning(pop)
    ControllerState m_stateBuffers[2];
    int             m_activeBuffer = 0;
    ControllerState m_lastState    = {};

    // Overlay components
    RadialMenu      m_radialMenu;

    // Toast queue
    std::mutex                      m_toastMutex;
    std::queue<ToastNotification>   m_pendingToasts;
    std::vector<ToastNotification>  m_activeToasts;

    // Render thread
    std::thread        m_renderThread;
    std::atomic<bool>  m_running{false};
    std::atomic<bool>  m_shown{false};

    float m_dpiScale = 1.0f;
};

} // namespace enjoystick::overlay
