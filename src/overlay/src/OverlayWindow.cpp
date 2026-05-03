#include "OverlayWindow_Impl.hpp"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dcomp.lib")

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

bool    OverlayWindowImpl::IsShown()  const noexcept { return m_shown.load(); }
HWND__* OverlayWindowImpl::GetHWND()  const noexcept { return m_hwnd; }
RadialMenu& OverlayWindowImpl::GetRadialMenu()        { return m_radialMenu; }

// ---------------------------------------------------------------------------
// PostState — lock-free double-buffer swap (input thread → render thread)
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
// Window + Direct2D initialisation
// ---------------------------------------------------------------------------

void OverlayWindowImpl::CreateWindowAndD2D() {
    // Find the target monitor by index
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
        // Fallback: primary monitor
        ctx.rect = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    }

    m_monitor    = ctx.result;
    m_monitorRect = { ctx.rect.left, ctx.rect.top, ctx.rect.right, ctx.rect.bottom };

    const UINT dpi = GetDpiForSystem();
    m_dpiScale = static_cast<float>(dpi) / 96.0f;

    // Register window class (idempotent: ignore ERROR_CLASS_ALREADY_EXISTS)
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

    if (!m_hwnd) throw std::runtime_error("OverlayWindow: CreateWindowEx failed");

    SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);

    // D2D factory
    D2D1_FACTORY_OPTIONS opts{};
#ifdef _DEBUG
    opts.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
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
}

void OverlayWindowImpl::DestroyWindowAndD2D() {
    m_dwriteFactory.Reset();
    m_d2dFactory.Reset();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
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

        // Consume pending state snapshot
        if (auto* p = m_pendingState.exchange(nullptr, std::memory_order_acquire)) {
            m_lastState    = *p;
            m_activeBuffer = 1 - m_activeBuffer;
        }

        // Drain pending toast queue under lock
        {
            std::lock_guard lock(m_toastMutex);
            while (!m_pendingToasts.empty()) {
                m_activeToasts.push_back(std::move(m_pendingToasts.front()));
                m_pendingToasts.pop();
            }
        }

        RenderFrame(dt * 0.001f);  // convert ms → seconds for animation

        // Sleep remainder of frame budget
        const float elapsed = std::chrono::duration<float, std::milli>(
            Clock::now() - prev).count();
        if (elapsed < targetMs) {
            const DWORD sleepMs = static_cast<DWORD>(targetMs - elapsed);
            if (sleepMs > 0) Sleep(sleepMs);
        }
    }
}

// ---------------------------------------------------------------------------
// RenderFrame  — Phase-1 implementation using ID2D1DCRenderTarget
//
// The DC-backed render target is created per-frame. This is intentional for
// Phase 1: it avoids the swap-chain complexity of DirectComposition while
// still delivering correct per-pixel alpha on the layered window.
// Phase 2 will replace this with a DXGI swap chain + DComp for lower latency.
// ---------------------------------------------------------------------------

void OverlayWindowImpl::RenderFrame(float deltaSeconds) {
    if (!m_hwnd || !m_d2dFactory) return;

    HDC hdc = GetDC(m_hwnd);
    if (!hdc) return;

    RECT wr{};
    GetClientRect(m_hwnd, &wr);
    const int w = wr.right - wr.left;
    const int h = wr.bottom - wr.top;
    if (w == 0 || h == 0) { ReleaseDC(m_hwnd, hdc); return; }

    // Create DC render target (per-frame; cheap for Phase-1 prototype)
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> rt;
    const D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    if (FAILED(m_d2dFactory->CreateDCRenderTarget(&rtp, rt.GetAddressOf()))) {
        ReleaseDC(m_hwnd, hdc);
        return;
    }

    if (FAILED(rt->BindDC(hdc, &wr))) {
        ReleaseDC(m_hwnd, hdc);
        return;
    }

    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));  // fully transparent

    // Pass rt.Get() as ID2D1RenderTarget* — upcast is safe (DCRenderTarget
    // inherits from ID2D1RenderTarget via ID2D1BitmapRenderTarget).
    m_radialMenu.Update(m_lastState, deltaSeconds);
    m_radialMenu.Draw(rt.Get(), m_dpiScale);

    DrawActiveIndicator(rt.Get());
    DrawToasts(rt.Get(), deltaSeconds);

    rt->EndDraw();
    ReleaseDC(m_hwnd, hdc);
}

// ---------------------------------------------------------------------------
// DrawActiveIndicator
//
// Phase 1: minimal glowing dot in the bottom-right corner.
// Indicates that EnjoyStick is active and a controller is connected.
// Opacity: 80% to stay unobtrusive.
// ---------------------------------------------------------------------------

void OverlayWindowImpl::DrawActiveIndicator(ID2D1RenderTarget* rt) {
    if (!m_config.showActiveIndicator) return;

    const float r    = 6.0f * m_dpiScale;
    const float marg = 16.0f * m_dpiScale;
    const float cx   = static_cast<float>(m_monitorRect.Width())  - marg;
    const float cy   = static_cast<float>(m_monitorRect.Height()) - marg;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    rt->CreateSolidColorBrush(
        D2D1::ColorF(0.34f, 0.31f, 0.98f, 0.80f),  // EnjoyStick violet
        brush.GetAddressOf());
    if (!brush) return;

    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r, r), brush.Get());
}

// ---------------------------------------------------------------------------
// DrawToasts
//
// Phase 1: simple white pill-shaped toast in the bottom-centre.
// Fade-out starts at 80% of duration.
// Phase 2: DWrite text, slide-in animation, background blur.
// ---------------------------------------------------------------------------

void OverlayWindowImpl::DrawToasts(ID2D1RenderTarget* rt, float deltaSeconds) {
    if (m_activeToasts.empty()) return;

    // Age all toasts
    for (auto& t : m_activeToasts)
        t.elapsed += deltaSeconds * 1000.0f;

    // Remove expired
    m_activeToasts.erase(
        std::remove_if(m_activeToasts.begin(), m_activeToasts.end(),
            [](const ToastNotification& t) {
                return t.elapsed >= static_cast<float>(t.durationMs);
            }),
        m_activeToasts.end());

    // Draw the most recent surviving toast
    if (m_activeToasts.empty()) return;
    const auto& toast = m_activeToasts.back();

    // Opacity: fade out over last 20% of duration
    const float frac    = toast.elapsed / static_cast<float>(toast.durationMs);
    const float opacity = (frac > 0.8f) ? (1.0f - (frac - 0.8f) / 0.2f) : 1.0f;

    // Background pill
    const float pw = 320.0f * m_dpiScale;
    const float ph = 44.0f  * m_dpiScale;
    const float px = (static_cast<float>(m_monitorRect.Width()) - pw) * 0.5f;
    const float py = static_cast<float>(m_monitorRect.Height()) - 80.0f * m_dpiScale;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    rt->CreateSolidColorBrush(
        D2D1::ColorF(0.10f, 0.10f, 0.14f, 0.85f * opacity),
        bgBrush.GetAddressOf());
    if (bgBrush) {
        D2D1_ROUNDED_RECT pill{};
        pill.rect    = D2D1::RectF(px, py, px + pw, py + ph);
        pill.radiusX = pill.radiusY = ph * 0.5f;
        rt->FillRoundedRectangle(pill, bgBrush.Get());
    }

    // Text label — Phase 2 will use IDWriteTextLayout for proper glyph rendering;
    // for now we use DrawText with a basic format object.
    if (!m_dwriteFactory) return;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        14.0f * m_dpiScale, L"en-us",
        fmt.GetAddressOf());
    if (!fmt) return;
    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush;
    rt->CreateSolidColorBrush(
        D2D1::ColorF(1.0f, 1.0f, 1.0f, opacity),
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
