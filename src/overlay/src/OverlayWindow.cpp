#include <enjoystick/overlay/OverlayWindow.hpp>

#include <stdexcept>
#include <cmath>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dcomp.lib")

namespace enjoystick::overlay {

static constexpr wchar_t kWindowClass[] = L"EnjoyStickOverlay";
static constexpr wchar_t kWindowTitle[] = L"Enjoystick Overlay";

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

OverlayWindow::OverlayWindow(Config config)
    : m_config(std::move(config)) {}

OverlayWindow::~OverlayWindow() {
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        UnregisterClassW(kWindowClass, m_config.hInstance);
    }
}

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------

bool OverlayWindow::Initialize() {
    return InitWindow() && InitD3D() && InitD2D() && InitDComp() && InitDWrite();
}

bool OverlayWindow::InitWindow() {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = m_config.hInstance;
    wc.lpszClassName = kWindowClass;
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);

    const int sw = GetSystemMetrics(SM_CXSCREEN);
    const int sh = GetSystemMetrics(SM_CYSCREEN);

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        kWindowClass, kWindowTitle,
        WS_POPUP,
        0, 0, sw, sh,
        nullptr, nullptr, m_config.hInstance, this);

    if (!m_hwnd) return false;

    // Transparent layered window
    SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    return true;
}

bool OverlayWindow::InitD3D() {
    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Format      = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc  = {1, 0};
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    scd.AlphaMode   = DXGI_ALPHA_MODE_PREMULTIPLIED;
    scd.Width       = static_cast<UINT>(GetSystemMetrics(SM_CXSCREEN));
    scd.Height      = static_cast<UINT>(GetSystemMetrics(SM_CYSCREEN));

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    ComPtr<ID3D11DeviceContext> ctx;
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        nullptr, 0, D3D11_SDK_VERSION,
        &m_d3dDevice, &fl, &ctx);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIDevice1> dxgiDev;
    m_d3dDevice.As(&dxgiDev);
    ComPtr<IDXGIAdapter> adapter;
    dxgiDev->GetAdapter(&adapter);
    ComPtr<IDXGIFactory2> factory;
    adapter->GetParent(IID_PPV_ARGS(&factory));

    hr = factory->CreateSwapChainForComposition(m_d3dDevice.Get(), &scd, nullptr, &m_swapChain);
    return SUCCEEDED(hr);
}

bool OverlayWindow::InitD2D() {
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        IID_PPV_ARGS(&m_d2dFactory));
    if (FAILED(hr)) return false;

    ComPtr<IDXGIDevice> dxgiDev;
    m_d3dDevice.As(&dxgiDev);
    hr = m_d2dFactory->CreateDevice(dxgiDev.Get(), &m_d2dDevice);
    if (FAILED(hr)) return false;

    hr = m_d2dDevice->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
        reinterpret_cast<ID2D1DeviceContext**>(m_d2dContext.GetAddressOf()));
    if (FAILED(hr)) return false;

    // Get back-buffer surface and bind as D2D render target
    ComPtr<IDXGISurface2> surface;
    m_swapChain->GetBuffer(0, IID_PPV_ARGS(&surface));

    D2D1_BITMAP_PROPERTIES1 bp{};
    bp.pixelFormat   = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
    bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    ComPtr<ID2D1Bitmap1> bmp;
    hr = m_d2dContext->CreateBitmapFromDxgiSurface(surface.Get(), &bp, &bmp);
    if (FAILED(hr)) return false;
    m_d2dContext->SetTarget(bmp.Get());

    // Brushes
    m_d2dContext->CreateSolidColorBrush({0.07f, 0.07f, 0.09f, 0.85f}, &m_brushHUDBg);
    m_d2dContext->CreateSolidColorBrush({0.95f, 0.95f, 0.97f, 1.00f}, &m_brushHUDText);
    m_d2dContext->CreateSolidColorBrush({0.05f, 0.05f, 0.07f, 0.92f}, &m_brushToastBg);
    m_d2dContext->CreateSolidColorBrush({0.95f, 0.95f, 0.97f, 1.00f}, &m_brushToastText);
    m_d2dContext->CreateSolidColorBrush({0.18f, 0.52f, 1.00f, 1.00f}, &m_brushAccent);  // Electric blue
    return true;
}

bool OverlayWindow::InitDComp() {
    ComPtr<IDXGIDevice> dxgiDev;
    m_d3dDevice.As(&dxgiDev);
    HRESULT hr = DCompositionCreateDevice(dxgiDev.Get(), IID_PPV_ARGS(&m_dcompDevice));
    if (FAILED(hr)) return false;

    hr = m_dcompDevice->CreateTargetForHwnd(m_hwnd, TRUE, &m_dcompTarget);
    if (FAILED(hr)) return false;

    hr = m_dcompDevice->CreateVisual(&m_dcompVisual);
    if (FAILED(hr)) return false;

    m_dcompVisual->SetContent(m_swapChain.Get());
    m_dcompTarget->SetRoot(m_dcompVisual.Get());
    m_dcompDevice->Commit();
    return true;
}

bool OverlayWindow::InitDWrite() {
    HRESULT hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory7),
        reinterpret_cast<IUnknown**>(m_dwFactory.GetAddressOf()));
    if (FAILED(hr)) return false;

    m_dwFactory->CreateTextFormat(
        L"Segoe UI Variable", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        14.0f, L"en-US", &m_fontHUD);

    m_dwFactory->CreateTextFormat(
        L"Segoe UI Variable", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        13.0f, L"en-US", &m_fontToast);
    return true;
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

void OverlayWindow::Render() {
    if (!m_d2dContext) return;

    // Compute delta time
    LARGE_INTEGER qpc, freq;
    QueryPerformanceCounter(&qpc);
    QueryPerformanceFrequency(&freq);
    const uint64_t now = static_cast<uint64_t>(qpc.QuadPart);
    const float dt = (m_lastRenderTick > 0)
        ? static_cast<float>(now - m_lastRenderTick) / static_cast<float>(freq.QuadPart)
        : 0.0f;
    m_lastRenderTick = now;

    // Tick toast timer
    if (m_toastRemainingMs > 0) {
        const uint32_t dtMs = static_cast<uint32_t>(dt * 1000.0f);
        m_toastRemainingMs = (dtMs >= m_toastRemainingMs) ? 0 : m_toastRemainingMs - dtMs;
    }

    m_d2dContext->BeginDraw();
    m_d2dContext->Clear({0.0f, 0.0f, 0.0f, 0.0f}); // fully transparent

    if (m_hudVisible)        DrawHUD(m_d2dContext.Get());
    if (m_toastRemainingMs)  DrawToast(m_d2dContext.Get());
    if (m_radialMenuVisible) DrawRadialMenuBackground(m_d2dContext.Get());

    m_d2dContext->EndDraw();
    m_swapChain->Present(1, 0);
    m_dcompDevice->Commit();
}

// ---------------------------------------------------------------------------
// Draw helpers
// ---------------------------------------------------------------------------

void OverlayWindow::DrawHUD(ID2D1DeviceContext5* dc) {
    const float sw = static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
    constexpr float kHudH  = 36.0f;
    constexpr float kHudW  = 320.0f;
    constexpr float kPad   = 12.0f;
    constexpr float kRadius = 8.0f;

    const D2D1_ROUNDED_RECT rrect = {
        {sw - kHudW - kPad, kPad, sw - kPad, kPad + kHudH},
        kRadius, kRadius
    };

    dc->FillRoundedRectangle(rrect, m_brushHUDBg.Get());

    // Accent left stripe
    const D2D1_ROUNDED_RECT accent = {
        {rrect.rect.left, rrect.rect.top, rrect.rect.left + 4.0f, rrect.rect.bottom},
        2.0f, 2.0f
    };
    dc->FillRoundedRectangle(accent, m_brushAccent.Get());

    // Mode label
    const std::wstring label = L"\U0001F3AE  " + m_modeLabel;
    dc->DrawText(
        label.c_str(), static_cast<UINT32>(label.size()),
        m_fontHUD.Get(),
        {rrect.rect.left + 14.0f, rrect.rect.top + 8.0f,
         rrect.rect.right - 8.0f, rrect.rect.bottom},
        m_brushHUDText.Get());
}

void OverlayWindow::DrawToast(ID2D1DeviceContext5* dc) {
    const float sw = static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
    const float sh = static_cast<float>(GetSystemMetrics(SM_CYSCREEN));
    constexpr float kW = 340.0f, kH = 52.0f, kPad = 16.0f, kR = 10.0f;

    const float opacity = std::min(1.0f,
        static_cast<float>(m_toastRemainingMs) / (m_config.fadeDurationMs * 0.001f * 1000.0f));

    dc->SetTransform(D2D1::Matrix3x2F::Identity());

    const D2D1_ROUNDED_RECT rr = {
        {sw - kW - kPad, sh - kH - kPad, sw - kPad, sh - kPad},
        kR, kR
    };

    // Fade via opacity layer
    dc->BeginLayer(D2D1::LayerParameters(), nullptr);
    m_brushToastBg->SetOpacity(opacity * 0.92f);
    dc->FillRoundedRectangle(rr, m_brushToastBg.Get());
    m_brushToastText->SetOpacity(opacity);
    dc->DrawText(
        m_toastMessage.c_str(), static_cast<UINT32>(m_toastMessage.size()),
        m_fontToast.Get(),
        {rr.rect.left + kPad, rr.rect.top + 15.0f, rr.rect.right - kPad, rr.rect.bottom},
        m_brushToastText.Get());
    dc->EndLayer();
    m_brushToastBg->SetOpacity(1.0f);
    m_brushToastText->SetOpacity(1.0f);
}

void OverlayWindow::DrawRadialMenuBackground(ID2D1DeviceContext5* dc) {
    const float sw = static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
    const float sh = static_cast<float>(GetSystemMetrics(SM_CYSCREEN));

    // Dim backdrop
    ComPtr<ID2D1SolidColorBrush> dimBrush;
    dc->CreateSolidColorBrush({0.0f, 0.0f, 0.0f, 0.55f}, &dimBrush);
    dc->FillRectangle({0, 0, sw, sh}, dimBrush.Get());

    // Centre circle placeholder (full radial rendering in OverlayRenderer)
    ComPtr<ID2D1SolidColorBrush> circleBrush;
    dc->CreateSolidColorBrush({0.07f, 0.07f, 0.10f, 0.95f}, &circleBrush);
    dc->FillEllipse({{sw * 0.5f, sh * 0.5f}, 180.0f, 180.0f}, circleBrush.Get());
}

// ---------------------------------------------------------------------------
// Public control
// ---------------------------------------------------------------------------

void OverlayWindow::ShowHUD(bool visible) { m_hudVisible = visible; }

void OverlayWindow::ShowToast(const std::wstring& message) {
    m_toastMessage    = message;
    m_toastRemainingMs = m_config.toastDurationMs;
}

void OverlayWindow::ShowRadialMenu(bool visible) { m_radialMenuVisible = visible; }

void OverlayWindow::SetModeLabel(const std::wstring& label) { m_modeLabel = label; }

void OverlayWindow::SetControllerConnected(uint8_t index, bool connected) {
    if (index < 4) m_controllerConnected[index] = connected;
}

bool OverlayWindow::PumpMessages() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return true;
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------

LRESULT CALLBACK OverlayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    OverlayWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<OverlayWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT OverlayWindow::HandleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(m_hwnd, msg, wp, lp);
    }
}

} // namespace enjoystick::overlay
