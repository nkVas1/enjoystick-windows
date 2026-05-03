#pragma once

#include <enjoystick/overlay/OverlayWindow.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dcomp.h>
#include <wrl/client.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <queue>
#include <string>

namespace enjoystick::overlay {

struct ToastNotification {
    std::wstring message;
    uint32_t     durationMs;
    float        elapsed = 0.0f;  ///< ms elapsed since shown
};

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
    void DrawHUD(ID2D1DeviceContext* dc, float deltaSeconds);
    void DrawActiveIndicator(ID2D1DeviceContext* dc);
    void DrawToasts(ID2D1DeviceContext* dc, float deltaSeconds);

    Config m_config;

    HWND   m_hwnd  = nullptr;
    HMONITOR m_monitor = nullptr;
    Rect   m_monitorRect = {};

    // D2D / DirectComposition
    Microsoft::WRL::ComPtr<ID2D1Factory1>          m_d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext>      m_d2dContext;
    Microsoft::WRL::ComPtr<IDWriteFactory>          m_dwriteFactory;
    Microsoft::WRL::ComPtr<IDCompositionDevice>     m_dcompDevice;
    Microsoft::WRL::ComPtr<IDCompositionTarget>     m_dcompTarget;
    Microsoft::WRL::ComPtr<IDCompositionVisual>     m_dcompVisual;

    // State (written from input thread, read from render thread)
    alignas(64) std::atomic<ControllerState*> m_pendingState{nullptr};
    ControllerState m_stateBuffers[2];
    int             m_activeBuffer = 0;

    // Overlay components
    RadialMenu      m_radialMenu;
    ControllerState m_lastState = {};

    // Toasts
    std::mutex                   m_toastMutex;
    std::queue<ToastNotification> m_pendingToasts;
    std::vector<ToastNotification> m_activeToasts;

    // Render thread
    std::thread        m_renderThread;
    std::atomic<bool>  m_running{false};
    std::atomic<bool>  m_shown{false};

    // DPI
    float m_dpiScale = 1.0f;
};

} // namespace enjoystick::overlay
