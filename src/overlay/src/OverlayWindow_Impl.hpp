#pragma once

// NOTE: WIN32_LEAN_AND_MEAN and NOMINMAX are injected by CMake
// target_compile_definitions for this module. Do NOT redefine them here.
#include <Windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dcomp.h>
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
/// OverlayWindowImpl
///
/// Render pipeline (Phase 1 — DC-backed):
///   GetDC(m_hwnd)
///   → ID2D1DCRenderTarget  (factory->CreateDCRenderTarget)
///   → BindDC / BeginDraw / ... / EndDraw
///   → ReleaseDC
///
/// DrawActiveIndicator and DrawToasts accept ID2D1RenderTarget* which is
/// the common base of ID2D1DCRenderTarget AND ID2D1DeviceContext, so the
/// same helpers work with both Phase-1 (DCRenderTarget) and the planned
/// Phase-2 (DeviceContext + DirectComposition swap chain) render paths.
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
    void RenderLoop();
    void RenderFrame(float deltaSeconds);

    // ---- Drawing helpers ------------------------------------------------
    // All helpers take ID2D1RenderTarget* — the common base of:
    //   • ID2D1DCRenderTarget   (Phase 1, used now)
    //   • ID2D1DeviceContext    (Phase 2, DirectComposition path)
    // This prevents C2664 when upgrading the render path.
    void DrawActiveIndicator(ID2D1RenderTarget* rt);
    void DrawToasts(ID2D1RenderTarget* rt, float deltaSeconds);

    Config m_config;

    HWND     m_hwnd    = nullptr;
    HMONITOR m_monitor = nullptr;
    Rect     m_monitorRect = {};

    // D2D / DirectWrite
    Microsoft::WRL::ComPtr<ID2D1Factory1>  m_d2dFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory> m_dwriteFactory;
    // Note: m_d2dContext (ID2D1DeviceContext) reserved for Phase-2.
    // Phase-1 creates a fresh ID2D1DCRenderTarget per frame.

    // State (written from input thread, read from render thread)
    // Two buffers + atomic pointer for lock-free hand-off.
    alignas(64) std::atomic<ControllerState*> m_pendingState{nullptr};
    ControllerState m_stateBuffers[2];
    int             m_activeBuffer = 0;

    // Overlay components
    RadialMenu      m_radialMenu;
    ControllerState m_lastState = {};

    // Toast queue
    std::mutex                      m_toastMutex;
    std::queue<ToastNotification>   m_pendingToasts;
    std::vector<ToastNotification>  m_activeToasts;

    // Render thread control
    std::thread        m_renderThread;
    std::atomic<bool>  m_running{false};
    std::atomic<bool>  m_shown{false};

    // DPI scale (set from GetDpiForSystem at window creation)
    float m_dpiScale = 1.0f;
};

} // namespace enjoystick::overlay
