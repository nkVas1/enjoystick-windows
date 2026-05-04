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
    m_startEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);  // auto-reset
}

OverlayWindowImpl::~OverlayWindowImpl() {
    Hide();
    if (m_startEvent) { CloseHandle(m_startEvent); m_startEvent = nullptr; }
}

// ---------------------------------------------------------------------------
// Show / Hide
// ---------------------------------------------------------------------------

void OverlayWindowImpl::Show() {
    if (m_running.exchange(true)) return;

    // Spin up render thread FIRST; it will create the HWND itself so that
    // the HWND's owning thread == the thread that pumps its messages.
    m_renderThread = std::thread([this] { RenderThread(); });

    // Wait until CreateWindowAndD2D() completes inside the render thread
    // before returning to the caller (avoids a race where the caller tries
    // to call PostState before the HWND exists).
    WaitForSingleObject(m_startEvent, 5000 /*ms*/);
    m_shown = true;
}

void OverlayWindowImpl::Hide() {
    if (!m_running.exchange(false)) return;
    // Wake the render thread's message loop
    if (m_hwnd) PostMessageW(m_hwnd, WM_QUIT, 0, 0);
    if (m_renderThread.joinable()) m_renderThread.join();
    m_shown = false;
}

bool          OverlayWindowImpl::IsShown()       const noexcept { return m_shown.load(); }
HWND__*       OverlayWindowImpl::GetHWND()       const noexcept { return m_hwnd; }
RadialMenu&   OverlayWindowImpl::GetRadialMenu()               { return m_radialMenu; }
SettingsMenu& OverlayWindowImpl::GetSettingsMenu()             { return m_settingsMenu; }

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
// RenderThread — owns the HWND lifecycle
// ---------------------------------------------------------------------------

void OverlayWindowImpl::RenderThread() {
    // All Win32 window operations (Create, Pump, Destroy) happen on THIS thread.
    CreateWindowAndD2D();
    SetEvent(m_startEvent);  // unblock Show()

    using Clock = std::chrono::high_resolution_clock;
    const float targetMs = 1000.0f / static_cast<float>(m_config.renderHz);
    auto prev = Clock::now();

    while (m_running.load(std::memory_order_relaxed)) {
        PumpMessages();  // non-blocking; drains queued WndProc messages

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

    DestroyWindowAndD2D();
}

// ---------------------------------------------------------------------------
// PumpMessages — non-blocking
// ---------------------------------------------------------------------------

void OverlayWindowImpl::PumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            m_running.store(false, std::memory_order_relaxed);
            return;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

// ---------------------------------------------------------------------------
// Window + D2D init (runs inside render thread)
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

    // Register window class (idempotent — RegisterClassEx returns 0 on duplicate)
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"EnjoyStickOverlay";
    RegisterClassExW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"EnjoyStickOverlay", L"EnjoyStick",
        WS_POPUP,
        ctx.rect.left, ctx.rect.top,
        m_monitorRect.Width(), m_monitorRect.Height(),
        nullptr, nullptr, wc.hInstance, this);

    if (!m_hwnd) return;  // non-fatal; overlay simply won't display

    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);

    // D2D factory (multi-threaded: render loop runs on this thread only,
    // but COM objects may be queried from other threads via GetFactory())
    D2D1_FACTORY_OPTIONS opts{};
    D2D1CreateFactory(
        D2D1_FACTORY_TYPE_MULTI_THREADED,
        __uuidof(ID2D1Factory1), &opts,
        reinterpret_cast<void**>(m_d2dFactory.GetAddressOf()));

    DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()));

    EnsureDib(m_monitorRect.Width(), m_monitorRect.Height());
    // m_renderTarget created lazily in EnsureRenderTarget() before first frame
}

void OverlayWindowImpl::DestroyWindowAndD2D() {
    m_renderTarget.Reset();
    DestroyDib();
    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        UnregisterClassW(L"EnjoyStickOverlay", GetModuleHandleW(nullptr));
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
    m_dibW     = w;
    m_dibH     = h;
    m_dibDirty = true;  // force RT re-bind
}

void OverlayWindowImpl::DestroyDib() {
    m_renderTarget.Reset();  // must release before deleting the DC
    if (m_hDib)  { DeleteObject(m_hDib);  m_hDib  = nullptr; }
    if (m_memDC) { DeleteDC(m_memDC);     m_memDC = nullptr; }
    m_dibW = m_dibH = 0;
    m_dibDirty = true;
}

// ---------------------------------------------------------------------------
// EnsureRenderTarget — creates or re-binds the cached DCRenderTarget
// ---------------------------------------------------------------------------

bool OverlayWindowImpl::EnsureRenderTarget() {
    if (!m_d2dFactory || !m_memDC || !m_hDib) return false;
    if (!m_dibDirty && m_renderTarget) return true;

    // Release old RT if any
    m_renderTarget.Reset();

    const D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> rt;
    if (FAILED(m_d2dFactory->CreateDCRenderTarget(&rtp, rt.GetAddressOf())))
        return false;

    RECT dibRect{ 0, 0, m_dibW, m_dibH };
    if (FAILED(rt->BindDC(m_memDC, &dibRect)))
        return false;

    m_renderTarget = rt;
    m_dibDirty     = false;
    return true;
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

    auto* self = reinterpret_cast<OverlayWindowImpl*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    if (self && msg == WM_SIZE) {
        const int w = LOWORD(lp);
        const int h = HIWORD(lp);
        if (w > 0 && h > 0 && (w != self->m_dibW || h != self->m_dibH)) {
            self->EnsureDib(w, h);  // m_dibDirty set inside
        }
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// RenderFrame
// ---------------------------------------------------------------------------

void OverlayWindowImpl::RenderFrame(float deltaSeconds) {
    if (!EnsureRenderTarget()) return;
    auto* rt = m_renderTarget.Get();

    const int w = m_dibW;
    const int h = m_dibH;
    if (w == 0 || h == 0) return;

    // Zero the DIB (clear to transparent black) using GDI — cheaper than D2D clear
    // for a full-screen overlay since the pattern is a solid ARGB(0,0,0,0).
    RECT dibRect{ 0, 0, w, h };
    FillRect(m_memDC, &dibRect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    const float sw = static_cast<float>(w);
    const float sh = static_cast<float>(h);

    // -----------------------------------------------------------------------
    // Input routing: SettingsMenu takes exclusive control when open
    // -----------------------------------------------------------------------
    const bool settingsOpen = m_settingsMenu.IsOpen();

    if (settingsOpen) {
        m_settingsMenu.Update(m_lastState, deltaSeconds);
        ControllerState empty{};
        m_radialMenu.Update(empty, deltaSeconds);
    } else {
        m_radialMenu.Update(m_lastState, deltaSeconds);
        ControllerState empty{};
        m_settingsMenu.Update(empty, deltaSeconds);
    }

    // Draw back-to-front
    m_radialMenu.Draw(rt, m_dwriteFactory.Get(), m_dpiScale, sw, sh);
    m_settingsMenu.Draw(rt, m_dwriteFactory.Get(), m_dpiScale, sw, sh);
    DrawHudMode(rt);
    DrawActiveIndicator(rt);
    DrawToasts(rt, deltaSeconds);

    if (rt->EndDraw() == D2DERR_RECREATE_TARGET) {
        // Device lost — force full recreation next frame
        m_renderTarget.Reset();
        m_dibDirty = true;
    }

    POINT ptSrc  = { 0, 0 };
    POINT ptDst  = { m_monitorRect.left, m_monitorRect.top };
    SIZE  szWnd  = { w, h };
    BLENDFUNCTION blend{};
    blend.BlendOp             = AC_SRC_OVER;
    blend.BlendFlags          = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat         = AC_SRC_ALPHA;

    UpdateLayeredWindow(
        m_hwnd, nullptr, &ptDst, &szWnd, m_memDC,
        &ptSrc, 0, &blend, ULW_ALPHA);
}

// ---------------------------------------------------------------------------
// DrawHudMode — permanent mode chip, bottom-left
// ---------------------------------------------------------------------------

void OverlayWindowImpl::DrawHudMode(ID2D1RenderTarget* rt) {
    if (!m_config.showActiveIndicator) return;
    if (!m_dwriteFactory) return;

    std::wstring label;
    {
        std::lock_guard lk(m_modeLabelMutex);
        label = m_modeLabel;
    }
    if (label.empty()) return;

    const float padX  = 14.0f * m_dpiScale;
    const float padY  =  8.0f * m_dpiScale;
    const float fSize = 12.0f * m_dpiScale;
    const float left  = 16.0f * m_dpiScale;
    const float bot   = static_cast<float>(m_dibH) - 16.0f * m_dpiScale;

    // Measure text
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, fSize, L"en-us", fmt.GetAddressOf());
    if (!fmt) return;

    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    m_dwriteFactory->CreateTextLayout(
        label.c_str(), static_cast<UINT32>(label.size()),
        fmt.Get(), 400.0f * m_dpiScale, 40.0f * m_dpiScale,
        layout.GetAddressOf());
    if (!layout) return;

    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);

    const float chipW = tm.width  + padX * 2.0f;
    const float chipH = tm.height + padY * 2.0f;
    const float chipX = left;
    const float chipY = bot - chipH;

    // Background pill
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bg;
        rt->CreateSolidColorBrush(
            D2D1::ColorF(0.06f, 0.06f, 0.10f, 0.82f), bg.GetAddressOf());
        if (bg) {
            D2D1_ROUNDED_RECT rr{};
            rr.rect    = D2D1::RectF(chipX, chipY, chipX + chipW, chipY + chipH);
            rr.radiusX = rr.radiusY = chipH * 0.5f;
            rt->FillRoundedRectangle(rr, bg.Get());
        }
    }

    // Text
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tb;
        rt->CreateSolidColorBrush(
            D2D1::ColorF(0.80f, 0.80f, 0.88f, 0.95f), tb.GetAddressOf());
        if (tb)
            rt->DrawTextLayout(
                D2D1::Point2F(chipX + padX, chipY + padY),
                layout.Get(), tb.Get());
    }
}

// ---------------------------------------------------------------------------
// DrawActiveIndicator — small glowing dot, bottom-right
// ---------------------------------------------------------------------------

void OverlayWindowImpl::DrawActiveIndicator(ID2D1RenderTarget* rt) {
    if (!m_config.showActiveIndicator) return;

    const float r    = 5.0f  * m_dpiScale;
    const float glow = 9.0f  * m_dpiScale;
    const float marg = 18.0f * m_dpiScale;
    const float cx   = static_cast<float>(m_dibW) - marg;
    const float cy   = static_cast<float>(m_dibH) - marg;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> glowBrush;
    rt->CreateSolidColorBrush(
        D2D1::ColorF(0.34f, 0.31f, 0.98f, 0.30f), glowBrush.GetAddressOf());
    if (glowBrush)
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), glow, glow), glowBrush.Get());

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dotBrush;
    rt->CreateSolidColorBrush(
        D2D1::ColorF(0.34f, 0.31f, 0.98f, 0.90f), dotBrush.GetAddressOf());
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
    // Ease-in fade: hold full opacity then fade out in last 20%
    float opacity = 1.0f;
    if (frac > 0.80f)
        opacity = 1.0f - (frac - 0.80f) / 0.20f;
    else if (frac < 0.05f)
        opacity = frac / 0.05f;  // brief fade-in

    const float pw = 360.0f * m_dpiScale;
    const float ph =  52.0f * m_dpiScale;
    const float px = (static_cast<float>(m_dibW) - pw) * 0.5f;
    const float py = static_cast<float>(m_dibH) - 108.0f * m_dpiScale;

    // Background
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
        rt->CreateSolidColorBrush(
            D2D1::ColorF(0.07f, 0.07f, 0.11f, 0.90f * opacity), bgBrush.GetAddressOf());
        if (bgBrush) {
            D2D1_ROUNDED_RECT pill{};
            pill.rect    = D2D1::RectF(px, py, px + pw, py + ph);
            pill.radiusX = pill.radiusY = ph * 0.5f;
            rt->FillRoundedRectangle(pill, bgBrush.Get());
        }
    }

    // Subtle border
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bd;
        rt->CreateSolidColorBrush(
            D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.07f * opacity), bd.GetAddressOf());
        if (bd) {
            D2D1_ROUNDED_RECT pill{};
            pill.rect    = D2D1::RectF(px + 0.5f, py + 0.5f, px + pw - 0.5f, py + ph - 0.5f);
            pill.radiusX = pill.radiusY = ph * 0.5f;
            Microsoft::WRL::ComPtr<ID2D1Factory> fac;
            rt->GetFactory(fac.GetAddressOf());
            if (fac) {
                Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> geo;
                fac->CreateRoundedRectangleGeometry(pill, geo.GetAddressOf());
                if (geo) rt->DrawGeometry(geo.Get(), bd.Get(), 1.0f);
            }
        }
    }

    if (!m_dwriteFactory) return;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        15.0f * m_dpiScale, L"en-us", fmt.GetAddressOf());
    if (!fmt) return;
    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush;
    rt->CreateSolidColorBrush(
        D2D1::ColorF(0.95f, 0.95f, 1.0f, opacity), textBrush.GetAddressOf());
    if (!textBrush) return;

    rt->DrawText(
        toast.message.c_str(),
        static_cast<UINT32>(toast.message.size()),
        fmt.Get(),
        D2D1::RectF(px, py, px + pw, py + ph),
        textBrush.Get());
}

} // namespace enjoystick::overlay
