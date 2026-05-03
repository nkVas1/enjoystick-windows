#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <dcomp.h>
#include <d3d11.h>
#include <d2d1_3.h>
#include <dwrite_3.h>
#include <wrl/client.h>

#include <enjoystick/shared/Types.hpp>
#include <functional>
#include <string>
#include <memory>
#include <atomic>

namespace enjoystick::overlay {

using Microsoft::WRL::ComPtr;

///
/// OverlayWindow — a layered, click-through Win32 window rendered via
/// Direct2D + DirectComposition for hardware-accelerated, tear-free compositing.
///
/// Features:
///   - Always-on-top, WS_EX_LAYERED | WS_EX_TRANSPARENT — never captures mouse/keyboard focus
///   - Animated opacity: fade-in on show, fade-out on hide (DirectComposition animation)
///   - HUD bar: controller icon, battery, current mode label
///   - Notification toast: bottom-right corner, auto-dismisses after N seconds
///   - Radial menu canvas: centre-screen, delegates drawing to IRadialMenuRenderer
///
class OverlayWindow {
public:
    struct Config {
        float   hudOpacity       = 0.88f;   ///< HUD bar opacity
        float   fadeDurationMs   = 180.0f;  ///< Fade in/out duration
        uint32_t toastDurationMs = 3000;    ///< Toast auto-dismiss
        HINSTANCE hInstance      = nullptr;
    };

    explicit OverlayWindow(Config config);
    ~OverlayWindow();

    // Non-copyable, non-movable (owns HWND and COM resources)
    OverlayWindow(const OverlayWindow&)            = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    /// Create and show the window. Call from main/UI thread.
    bool Initialize();

    /// Pump the message loop for this window. Returns false when WM_QUIT is posted.
    bool PumpMessages();

    /// Show/hide the HUD bar.
    void ShowHUD(bool visible);

    /// Show a notification toast.
    void ShowToast(const std::wstring& message);

    /// Show/hide the radial menu canvas.
    void ShowRadialMenu(bool visible);

    /// Update the mode label shown in the HUD (e.g., "Cursor" / "Gamepad" / "Media").
    void SetModeLabel(const std::wstring& label);

    /// Update controller connection indicator.
    void SetControllerConnected(uint8_t index, bool connected);

    [[nodiscard]] HWND GetHWND() const noexcept { return m_hwnd; }

    /// Trigger a single repaint frame (call from render loop).
    void Render();

private:
    bool InitWindow();
    bool InitD3D();
    bool InitD2D();
    bool InitDComp();
    bool InitDWrite();

    void DrawHUD(ID2D1DeviceContext5* dc);
    void DrawToast(ID2D1DeviceContext5* dc);
    void DrawRadialMenuBackground(ID2D1DeviceContext5* dc);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    Config m_config;
    HWND   m_hwnd = nullptr;

    // D3D / D2D / DWrite / DComp
    ComPtr<ID3D11Device>            m_d3dDevice;
    ComPtr<IDXGISwapChain1>         m_swapChain;
    ComPtr<ID2D1Factory7>           m_d2dFactory;
    ComPtr<ID2D1Device6>            m_d2dDevice;
    ComPtr<ID2D1DeviceContext5>     m_d2dContext;
    ComPtr<IDWriteFactory7>         m_dwFactory;
    ComPtr<IDWriteTextFormat>       m_fontHUD;
    ComPtr<IDWriteTextFormat>       m_fontToast;
    ComPtr<IDCompositionDevice>     m_dcompDevice;
    ComPtr<IDCompositionTarget>     m_dcompTarget;
    ComPtr<IDCompositionVisual>     m_dcompVisual;

    // Brushes
    ComPtr<ID2D1SolidColorBrush> m_brushHUDBg;
    ComPtr<ID2D1SolidColorBrush> m_brushHUDText;
    ComPtr<ID2D1SolidColorBrush> m_brushToastBg;
    ComPtr<ID2D1SolidColorBrush> m_brushToastText;
    ComPtr<ID2D1SolidColorBrush> m_brushAccent;

    // State
    bool         m_hudVisible          = false;
    bool         m_radialMenuVisible   = false;
    std::wstring m_modeLabel           = L"Cursor";
    std::wstring m_toastMessage;
    uint32_t     m_toastRemainingMs    = 0;
    bool         m_controllerConnected[4] = {};

    uint64_t     m_lastRenderTick      = 0;
};

} // namespace enjoystick::overlay
