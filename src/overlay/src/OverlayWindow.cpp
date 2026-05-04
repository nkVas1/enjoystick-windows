#include "OverlayWindow_Impl.hpp"
#include "Overlay_Theme.hpp"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace enjoystick::overlay {

std::unique_ptr<OverlayWindow> OverlayWindow::Create(Config config) {
    return std::make_unique<OverlayWindowImpl>(std::move(config));
}

OverlayWindowImpl::OverlayWindowImpl(Config config)
    : m_config(std::move(config))
{
    m_stateBuffers[0] = {};
    m_stateBuffers[1] = {};
    m_startEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

OverlayWindowImpl::~OverlayWindowImpl() {
    Hide();
    if (m_startEvent) { CloseHandle(m_startEvent); m_startEvent = nullptr; }
}

void OverlayWindowImpl::Show() {
    if (m_running.exchange(true)) return;
    m_renderThread = std::thread([this] { RenderThread(); });
    WaitForSingleObject(m_startEvent, 5000);
    m_shown = true;
}

void OverlayWindowImpl::Hide() {
    if (!m_running.exchange(false)) return;
    if (m_hwnd) PostMessageW(m_hwnd, WM_QUIT, 0, 0);
    if (m_renderThread.joinable()) m_renderThread.join();
    m_shown = false;
}

bool              OverlayWindowImpl::IsShown()           const noexcept { return m_shown.load(); }
HWND__*           OverlayWindowImpl::GetHWND()           const noexcept { return m_hwnd; }
RadialMenu&       OverlayWindowImpl::GetRadialMenu()                    { return m_radialMenu; }
SettingsMenu&     OverlayWindowImpl::GetSettingsMenu()                  { return m_settingsMenu; }
VirtualKeyboard&  OverlayWindowImpl::GetVirtualKeyboard()               { return m_keyboard; }

void OverlayWindowImpl::SetModeLabel(std::wstring label) {
    std::lock_guard lk(m_modeLabelMutex);
    m_modeLabel = std::move(label);
}

void OverlayWindowImpl::PostState(const ControllerState& state) {
    const int next = 1 - m_activeBuffer;
    m_stateBuffers[next] = state;
    m_pendingState.store(&m_stateBuffers[next], std::memory_order_release);
}

void OverlayWindowImpl::ShowToast(std::wstring message, uint32_t durationMs) {
    std::lock_guard lock(m_toastMutex);
    m_pendingToasts.push({std::move(message), durationMs, 0.0f});
}

void OverlayWindowImpl::RenderThread() {
    CreateWindowAndD2D();
    SetEvent(m_startEvent);

    using Clock = std::chrono::high_resolution_clock;
    const float targetMs = 1000.0f / static_cast<float>(m_config.renderHz);
    auto prev = Clock::now();

    while (m_running.load(std::memory_order_relaxed)) {
        PumpMessages();

        const auto now = Clock::now();
        const float dt = std::chrono::duration<float, std::milli>(now - prev).count();
        prev = now;

        if (auto* p = m_pendingState.exchange(nullptr, std::memory_order_acquire)) {
            m_lastState    = *p;
            m_activeBuffer = 1 - m_activeBuffer;
        }

        {
            std::lock_guard lock(m_toastMutex);
            while (!m_pendingToasts.empty()) {
                m_activeToasts.push_back(std::move(m_pendingToasts.front()));
                m_pendingToasts.pop();
            }
        }

        RenderFrame(dt * 0.001f);

        const float elapsed = std::chrono::duration<float, std::milli>(
            Clock::now() - prev).count();
        if (elapsed < targetMs) {
            const DWORD sleepMs = static_cast<DWORD>(targetMs - elapsed);
            if (sleepMs > 0) Sleep(sleepMs);
        }
    }

    DestroyWindowAndD2D();
}

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

void OverlayWindowImpl::CreateWindowAndD2D() {
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

    if (!ctx.result)
        ctx.rect = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };

    m_monitor     = ctx.result;
    m_monitorRect = { ctx.rect.left, ctx.rect.top, ctx.rect.right, ctx.rect.bottom };

    const UINT dpi = GetDpiForSystem();
    m_dpiScale = static_cast<float>(dpi) / 96.0f;

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

    if (!m_hwnd) return;
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);

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

void OverlayWindowImpl::EnsureDib(int w, int h) {
    if (m_memDC && m_dibW == w && m_dibH == h) return;
    DestroyDib();

    HDC screenDC = GetDC(nullptr);
    m_memDC = CreateCompatibleDC(screenDC);
    ReleaseDC(nullptr, screenDC);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       =  w;
    bmi.bmiHeader.biHeight      = -h;
    bmi.bmiHeader.biPlanes      =  1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    m_hDib = CreateDIBSection(m_memDC, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    if (!m_hDib) return;

    SelectObject(m_memDC, m_hDib);
    m_dibW     = w;
    m_dibH     = h;
    m_dibDirty = true;
}

void OverlayWindowImpl::DestroyDib() {
    m_renderTarget.Reset();
    if (m_hDib)  { DeleteObject(m_hDib);  m_hDib  = nullptr; }
    if (m_memDC) { DeleteDC(m_memDC);     m_memDC = nullptr; }
    m_dibW = m_dibH = 0;
    m_dibDirty = true;
}

bool OverlayWindowImpl::EnsureRenderTarget() {
    if (!m_d2dFactory || !m_memDC || !m_hDib) return false;
    if (!m_dibDirty && m_renderTarget) return true;

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

LRESULT CALLBACK OverlayWindowImpl::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<OverlayWindowImpl*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self && msg == WM_SIZE) {
        const int w = LOWORD(lp), h = HIWORD(lp);
        if (w > 0 && h > 0 && (w != self->m_dibW || h != self->m_dibH))
            self->EnsureDib(w, h);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// RenderFrame
// ---------------------------------------------------------------------------

void OverlayWindowImpl::RenderFrame(float deltaSeconds) {
    if (!EnsureRenderTarget()) return;
    auto* rt = m_renderTarget.Get();
    const int w = m_dibW, h = m_dibH;
    if (w == 0 || h == 0) return;

    RECT dibRect{ 0, 0, w, h };
    FillRect(m_memDC, &dibRect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    // ---- Input isolation -------------------------------------------------
    // Priority: radial > keyboard > settings.
    // Whichever is active receives real input; others receive empty state.
    const bool radialActive   = m_radialMenu.IsVisible();
    const bool keyboardActive = m_keyboard.IsOpen();
    const bool settingsActive = m_settingsMenu.IsOpen();

    static const ControllerState kEmpty{};

    if (radialActive) {
        m_radialMenu.Update(m_lastState, deltaSeconds);
        m_keyboard.Update(kEmpty, deltaSeconds);
        m_settingsMenu.Update(kEmpty, deltaSeconds);
    } else if (keyboardActive) {
        m_radialMenu.Update(kEmpty, deltaSeconds);
        m_keyboard.Update(m_lastState, deltaSeconds);
        m_settingsMenu.Update(kEmpty, deltaSeconds);
    } else if (settingsActive) {
        m_radialMenu.Update(kEmpty, deltaSeconds);
        m_keyboard.Update(kEmpty, deltaSeconds);
        m_settingsMenu.Update(m_lastState, deltaSeconds);
    } else {
        m_radialMenu.Update(kEmpty, deltaSeconds);
        m_keyboard.Update(kEmpty, deltaSeconds);
        m_settingsMenu.Update(kEmpty, deltaSeconds);
    }

    // ---- Draw order: radial, settings, keyboard, hud, toasts ---------------
    m_radialMenu.Draw(rt, m_dwriteFactory.Get(), m_dpiScale,
                      static_cast<float>(w), static_cast<float>(h));
    m_settingsMenu.Draw(rt, m_dwriteFactory.Get(), m_dpiScale,
                        static_cast<float>(w), static_cast<float>(h));
    m_keyboard.Draw(rt, m_dwriteFactory.Get(), m_dpiScale,
                    static_cast<float>(w), static_cast<float>(h));
    DrawHudMode(rt);
    DrawActiveIndicator(rt);
    DrawToasts(rt, deltaSeconds);

    if (rt->EndDraw() == D2DERR_RECREATE_TARGET) {
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
    UpdateLayeredWindow(m_hwnd, nullptr, &ptDst, &szWnd, m_memDC,
                        &ptSrc, 0, &blend, ULW_ALPHA);
}

// ---------------------------------------------------------------------------
// DrawActiveIndicator  — triple concentric gold halos
// ---------------------------------------------------------------------------

void OverlayWindowImpl::DrawActiveIndicator(ID2D1RenderTarget* rt) {
    if (!m_config.showActiveIndicator) return;

    const float s  = m_dpiScale;
    const float cx = static_cast<float>(m_dibW) - 22.0f * s;
    const float cy = static_cast<float>(m_dibH) - 22.0f * s;

    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldGlow(0.22f), b.GetAddressOf());
        if (b) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy), 18*s, 18*s), b.Get());
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldShadow(0.55f), b.GetAddressOf());
        if (b) rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy), 13*s, 13*s), b.Get(), 1.0f*s);
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldMid(0.80f), b.GetAddressOf());
        if (b) rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy), 8*s, 8*s), b.Get(), 1.2f*s);
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldHi(0.95f), b.GetAddressOf());
        if (b) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy), 4*s, 4*s), b.Get());
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::White(0.60f), b.GetAddressOf());
        if (b) rt->FillEllipse(
                   D2D1::Ellipse(D2D1::Point2F(cx - 1.2f*s, cy - 1.2f*s),
                                 1.5f*s, 1.5f*s), b.Get());
    }
}

// ---------------------------------------------------------------------------
// DrawHudMode  — obsidian pill with gold left-border accent
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

    const float s     = m_dpiScale;
    const float padX  = 14.0f * s;
    const float padY  =  7.0f * s;
    const float fSize = 11.5f * s;
    const float left  = 16.0f * s;
    const float bot   = static_cast<float>(m_dibH) - 18.0f * s;
    const float accentW = 3.0f * s;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, fSize, L"en-us", fmt.GetAddressOf());
    if (!fmt) return;

    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    m_dwriteFactory->CreateTextLayout(
        label.c_str(), static_cast<UINT32>(label.size()),
        fmt.Get(), 420.0f * s, 40.0f * s, layout.GetAddressOf());
    if (!layout) return;

    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);

    const float chipW = tm.width  + padX * 2.0f + accentW;
    const float chipH = tm.height + padY * 2.0f;
    const float chipX = left;
    const float chipY = bot - chipH;
    const float r     = chipH * 0.38f;

    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::DeepVoid(0.88f), b.GetAddressOf());
        if (b) {
            D2D1_ROUNDED_RECT rr{};
            rr.rect    = D2D1::RectF(chipX, chipY, chipX + chipW, chipY + chipH);
            rr.radiusX = rr.radiusY = r;
            rt->FillRoundedRectangle(rr, b.Get());
        }
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldMid(0.90f), b.GetAddressOf());
        if (b) {
            D2D1_ROUNDED_RECT bar{};
            bar.rect    = D2D1::RectF(chipX, chipY + r * 0.4f,
                                      chipX + accentW, chipY + chipH - r * 0.4f);
            bar.radiusX = bar.radiusY = accentW * 0.5f;
            rt->FillRoundedRectangle(bar, b.Get());
        }
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldShadow(0.40f), b.GetAddressOf());
        if (b) {
            D2D1_ROUNDED_RECT rr{};
            rr.rect    = D2D1::RectF(chipX + 0.5f, chipY + 0.5f,
                                     chipX + chipW - 0.5f, chipY + chipH - 0.5f);
            rr.radiusX = rr.radiusY = r;
            rt->DrawRoundedRectangle(rr, b.Get(), 0.8f);
        }
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SilverLt(0.92f), b.GetAddressOf());
        if (b)
            rt->DrawTextLayout(
                D2D1::Point2F(chipX + accentW + padX, chipY + padY),
                layout.Get(), b.Get());
    }
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
    float opacity = 1.0f;
    if      (frac > 0.82f) opacity = 1.0f - (frac - 0.82f) / 0.18f;
    else if (frac < 0.05f) opacity = frac / 0.05f;
    opacity = std::max(0.0f, opacity);

    const float s  = m_dpiScale;
    const float pw = 380.0f * s;
    const float ph =  52.0f * s;
    const float px = (static_cast<float>(m_dibW) - pw) * 0.5f;
    const float py = static_cast<float>(m_dibH) - 112.0f * s;
    const float rr = ph * 0.5f;

    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::DeepVoid(0.92f * opacity), b.GetAddressOf());
        if (b) {
            D2D1_ROUNDED_RECT pill{ D2D1::RectF(px,py,px+pw,py+ph), rr, rr };
            rt->FillRoundedRectangle(pill, b.Get());
        }
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldMid(0.70f * opacity), b.GetAddressOf());
        if (b)
            rt->DrawLine(
                D2D1::Point2F(px + rr, py + 0.8f),
                D2D1::Point2F(px + pw - rr, py + 0.8f),
                b.Get(), 1.2f * s);
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldShadow(0.30f * opacity), b.GetAddressOf());
        if (b) {
            D2D1_ROUNDED_RECT pill{ D2D1::RectF(px+0.5f,py+0.5f,px+pw-0.5f,py+ph-0.5f), rr, rr };
            rt->DrawRoundedRectangle(pill, b.Get(), 0.8f);
        }
    }
    if (!m_dwriteFactory) return;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 14.5f * s, L"en-us", fmt.GetAddressOf());
    if (!fmt) return;
    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tb;
    rt->CreateSolidColorBrush(Tok::SilverLt(0.95f * opacity), tb.GetAddressOf());
    if (!tb) return;
    rt->DrawText(
        toast.message.c_str(),
        static_cast<UINT32>(toast.message.size()),
        fmt.Get(),
        D2D1::RectF(px, py, px + pw, py + ph),
        tb.Get());
}

} // namespace enjoystick::overlay
