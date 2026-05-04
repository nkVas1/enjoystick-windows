#include "OverlayWindow_Impl.hpp"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

#include <stdexcept>
#include <algorithm>
#include <chrono>

namespace enjoystick::overlay {

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::unique_ptr<OverlayWindow> OverlayWindow::Create(Config config) {
    return std::make_unique<OverlayWindowImpl>(std::move(config));
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

OverlayWindowImpl::OverlayWindowImpl(Config config)
    : m_config(std::move(config))
{
    m_stateBuffers[0] = {};
    m_stateBuffers[1] = {};
}

OverlayWindowImpl::~OverlayWindowImpl() {
    Hide();
}

// ---------------------------------------------------------------------------
// Show / Hide
// ---------------------------------------------------------------------------

void OverlayWindowImpl::Show() {
    if (m_running.exchange(true)) return;
    CreateWindowAndD2D();
    m_shown = true;
    m_renderThread = std::thread([this] { RenderLoop(); });
}

void OverlayWindowImpl::Hide() {
    if (!m_running.exchange(false)) return;
    if (m_renderThread.joinable()) m_renderThread.join();
    DestroyWindowAndD2D();
    m_shown = false;
}

bool    OverlayWindowImpl::IsShown()     const noexcept { return m_shown.load(); }
HWND__* OverlayWindowImpl::GetHWND()     const noexcept { return m_hwnd; }
RadialMenu& OverlayWindowImpl::GetRadialMenu()         { return m_radialMenu; }

// ---------------------------------------------------------------------------
// PostState
// ---------------------------------------------------------------------------

void OverlayWindowImpl::PostState(const ControllerState& state) {
    const int next = 1 - m_activeBuffer;
    m_stateBuffers[next] = state;
    m_pendingState.store(&m_stateBuffers[next], std::memory_order_release);
}

// ---------------------------------------------------------------------------
// ShowToast
// ---------------------------------------------------------------------------

void OverlayWindowImpl::ShowToast(std::wstring message, uint32_t durationMs) {
    std::lock_guard lock(m_toastMutex);
    m_pendingToasts.push({std::move(message), durationMs, 0.0f});
}

// ---------------------------------------------------------------------------
// Window + D2D init
// ---------------------------------------------------------------------------

void OverlayWindowImpl::CreateWindowAndD2D() {
    // Enumerate target monitor
    struct MonitorEnum {
        uint32_t target;
        uint32_t current = 0;
        HMONITOR result  = nullptr;
        RECT     rect    = {};
    } ctx{m_config.monitorIndex};

    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR hmon, HDC, LPRECT rc, LPARAM lp) -> BOOL {
            auto& e = *reinterpret_cast<MonitorEnum*>(lp);
            if (e.current++ == e.target) { e.result = hmon; e.rect = *rc; return FALSE; }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));

    if (!ctx.result) {
        ctx.rect = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    }

    m_monitor     = ctx.result;
    m_monitorRect = { ctx.rect.left, ctx.rect.top, ctx.rect.right, ctx.rect.bottom };

    const UINT dpi = GetDpiForSystem();
    m_dpiScale = static_cast<float>(dpi) / 96.0f;

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"EnjoyStickOverlay";
    RegisterClassExW(&wc);

    // WS_EX_LAYERED: required for UpdateLayeredWindow.
    // WS_EX_TRANSPARENT: click-through (input passes to windows below).
    // WS_EX_NOACTIVATE:  never steals focus.
    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"EnjoyStickOverlay", L"EnjoyStick",
        WS_POPUP,
        ctx.rect.left, ctx.rect.top,
        m_monitorRect.Width(), m_monitorRect.Height(),
        nullptr, nullptr, wc.hInstance, this);

    if (!m_hwnd) throw std::runtime_error("OverlayWindow: CreateWindowEx failed");

    // DO NOT call SetLayeredWindowAttributes here.
    // UpdateLayeredWindow owns the window's visual appearance instead.
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);

    // D2D factory (multi-threaded: render loop runs on its own thread)
    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_MULTI_THREADED,
            __uuidof(ID2D1Factory1), &opts,
            reinterpret_cast<void**>(m_d2dFactory.GetAddressOf()))))
    {
        throw std::runtime_error("OverlayWindow: D2D1CreateFactory failed");
    }

    // DirectWrite factory
    if (FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()))))
    {
        throw std::runtime_error("OverlayWindow: DWriteCreateFactory failed");
    }

    // Pre-create the DIB for the full monitor size
    EnsureDib(m_monitorRect.Width(), m_monitorRect.Height());
}

void OverlayWindowImpl::DestroyWindowAndD2D() {
    DestroyDib();
    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

// ---------------------------------------------------------------------------
// DIB management
// ---------------------------------------------------------------------------

void OverlayWindowImpl::EnsureDib(int w, int h) {
    if (m_memDC && m_dibW == w && m_dibH == h) return;
    DestroyDib();

    HDC screenDC = GetDC(nullptr);
    m_memDC = CreateCompatibleDC(screenDC);
    ReleaseDC(nullptr, screenDC);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       =  w;
    bmi.bmiHeader.biHeight      = -h;  // top-down
    bmi.bmiHeader.biPlanes      =  1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    m_hDib = CreateDIBSection(m_memDC, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    if (!m_hDib) return;

    SelectObject(m_memDC, m_hDib);
    m_dibW = w;
    m_dibH = h;
}

void OverlayWindowImpl::DestroyDib() {
    if (m_hDib)  { DeleteObject(m_hDib);  m_hDib  = nullptr; }
    if (m_memDC) { DeleteDC(m_memDC);     m_memDC = nullptr; }
    m_dibW = m_dibH = 0;
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------

LRESULT CALLBACK OverlayWindowImpl::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Render loop
// ---------------------------------------------------------------------------

void OverlayWindowImpl::RenderLoop() {
    using Clock = std::chrono::high_resolution_clock;
    const float targetMs = 1000.0f / static_cast<float>(m_config.renderHz);
    auto prev = Clock::now();

    while (m_running.load(std::memory_order_relaxed)) {
        const auto now = Clock::now();
        const float dt = std::chrono::duration<float, std::milli>(now - prev).count();
        prev = now;

        // Consume latest state snapshot
        if (auto* p = m_pendingState.exchange(nullptr, std::memory_order_acquire)) {
            m_lastState    = *p;
            m_activeBuffer = 1 - m_activeBuffer;
        }

        // Drain pending toasts
        {
            std::lock_guard lock(m_toastMutex);
            while (!m_pendingToasts.empty()) {
                m_activeToasts.push_back(std::move(m_pendingToasts.front()));
                m_pendingToasts.pop();
            }
        }

        RenderFrame(dt * 0.001f);  // ms → seconds

        const float elapsed = std::chrono::duration<float, std::milli>(
            Clock::now() - prev).count();
        if (elapsed < targetMs) {
            const DWORD sleepMs = static_cast<DWORD>(targetMs - elapsed);
            if (sleepMs > 0) Sleep(sleepMs);
        }
    }
}

// ---------------------------------------------------------------------------
// RenderFrame — DIB + UpdateLayeredWindow path
//
// Per-pixel alpha works correctly here because UpdateLayeredWindow(ULW_ALPHA)
// reads each pixel's alpha channel directly from the DIB. The D2D clear to
// (0,0,0,0) leaves those pixels fully transparent on-screen.
// ---------------------------------------------------------------------------

void OverlayWindowImpl::RenderFrame(float deltaSeconds) {
    if (!m_hwnd || !m_d2dFactory || !m_memDC || !m_hDib) return;

    const int w = m_dibW;
    const int h = m_dibH;
    if (w == 0 || h == 0) return;

    // Zero the DIB before each frame (clear to transparent black)
    RECT dibRect{ 0, 0, w, h };
    FillRect(m_memDC, &dibRect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    // Black fill sets alpha=0 in the DIB; D2D will overwrite with premultiplied values.

    // Create DC render target bound to the memory DC
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> rt;
    const D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    if (FAILED(m_d2dFactory->CreateDCRenderTarget(&rtp, rt.GetAddressOf()))) return;
    if (FAILED(rt->BindDC(m_memDC, &dibRect))) return;

    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));  // fully transparent

    m_radialMenu.Update(m_lastState, deltaSeconds);
    m_radialMenu.Draw(rt.Get(), m_dwriteFactory.Get(), m_dpiScale,
                      static_cast<float>(w), static_cast<float>(h));

    DrawActiveIndicator(rt.Get());
    DrawToasts(rt.Get(), deltaSeconds);

    rt->EndDraw();

    // Push the DIB to the layered window with per-pixel alpha
    POINT ptSrc  = { 0, 0 };
    POINT ptDst  = { m_monitorRect.left, m_monitorRect.top };
    SIZE  szWnd  = { w, h };
    BLENDFUNCTION blend{};
    blend.BlendOp             = AC_SRC_OVER;
    blend.BlendFlags          = 0;
    blend.SourceConstantAlpha = 255;   // use per-pixel alpha
    blend.AlphaFormat         = AC_SRC_ALPHA;

    UpdateLayeredWindow(
        m_hwnd,
        nullptr,     // use screen DC
        &ptDst,
        &szWnd,
        m_memDC,
        &ptSrc,
        0,
        &blend,
        ULW_ALPHA);
}

// ---------------------------------------------------------------------------
// DrawActiveIndicator
//
// Small glowing dot (bottom-right corner) that confirms EnjoyStick is running.
// Outer glow ring + filled centre.
// ---------------------------------------------------------------------------

void OverlayWindowImpl::DrawActiveIndicator(ID2D1RenderTarget* rt) {
    if (!m_config.showActiveIndicator) return;

    const float r    = 5.0f  * m_dpiScale;
    const float glow = 9.0f  * m_dpiScale;
    const float marg = 18.0f * m_dpiScale;
    const float cx   = static_cast<float>(m_dibW) - marg;
    const float cy   = static_cast<float>(m_dibH) - marg;

    // Glow ring
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> glowBrush;
    rt->CreateSolidColorBrush(
        D2D1::ColorF(0.34f, 0.31f, 0.98f, 0.30f),
        glowBrush.GetAddressOf());
    if (glowBrush)
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), glow, glow), glowBrush.Get());

    // Solid centre
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dotBrush;
    rt->CreateSolidColorBrush(
        D2D1::ColorF(0.34f, 0.31f, 0.98f, 0.90f),
        dotBrush.GetAddressOf());
    if (dotBrush)
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), dotBrush.Get());
}

// ---------------------------------------------------------------------------
// DrawToasts
// ---------------------------------------------------------------------------

void OverlayWindowImpl::DrawToasts(ID2D1RenderTarget* rt, float deltaSeconds) {
    if (m_activeToasts.empty()) return;

    for (auto& t : m_activeToasts)
        t.elapsed += deltaSeconds * 1000.0f;

    m_activeToasts.erase(
        std::remove_if(m_activeToasts.begin(), m_activeToasts.end(),
            [](const ToastNotification& t) {
                return t.elapsed >= static_cast<float>(t.durationMs);
            }),
        m_activeToasts.end());

    if (m_activeToasts.empty()) return;
    const auto& toast = m_activeToasts.back();

    const float frac    = toast.elapsed / static_cast<float>(toast.durationMs);
    const float opacity = (frac > 0.80f) ? (1.0f - (frac - 0.80f) / 0.20f) : 1.0f;

    const float pw = 340.0f * m_dpiScale;
    const float ph = 48.0f  * m_dpiScale;
    const float px = (static_cast<float>(m_dibW) - pw) * 0.5f;
    const float py = static_cast<float>(m_dibH) - 96.0f * m_dpiScale;

    // Background pill
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    rt->CreateSolidColorBrush(
        D2D1::ColorF(0.08f, 0.08f, 0.12f, 0.88f * opacity),
        bgBrush.GetAddressOf());
    if (bgBrush) {
        D2D1_ROUNDED_RECT pill{};
        pill.rect    = D2D1::RectF(px, py, px + pw, py + ph);
        pill.radiusX = pill.radiusY = ph * 0.5f;
        rt->FillRoundedRectangle(pill, bgBrush.Get());
    }

    if (!m_dwriteFactory) return;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        15.0f * m_dpiScale, L"en-us",
        fmt.GetAddressOf());
    if (!fmt) return;
    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush;
    rt->CreateSolidColorBrush(
        D2D1::ColorF(0.96f, 0.96f, 1.0f, opacity),
        textBrush.GetAddressOf());
    if (!textBrush) return;

    rt->DrawText(
        toast.message.c_str(),
        static_cast<UINT32>(toast.message.size()),
        fmt.Get(),
        D2D1::RectF(px, py, px + pw, py + ph),
        textBrush.Get());
}

} // namespace enjoystick::overlay
