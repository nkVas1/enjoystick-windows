#include "OverlayWindow_Impl.hpp"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dcomp.lib")

#include <stdexcept>
#include <chrono>

namespace enjoystick::overlay {

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::unique_ptr<OverlayWindow> OverlayWindow::Create(Config config) {
    return std::make_unique<OverlayWindowImpl>(std::move(config));
}

// ---------------------------------------------------------------------------
// Impl — construction / destruction
// ---------------------------------------------------------------------------

OverlayWindowImpl::OverlayWindowImpl(Config config)
    : m_config(std::move(config)) {
    // Pre-size state buffers
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
    if (m_running.exchange(true)) return;  // already running

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

bool OverlayWindowImpl::IsShown() const noexcept { return m_shown.load(); }
HWND__* OverlayWindowImpl::GetHWND() const noexcept { return m_hwnd; }
RadialMenu& OverlayWindowImpl::GetRadialMenu() { return m_radialMenu; }

// ---------------------------------------------------------------------------
// PostState — lock-free double-buffer swap
// ---------------------------------------------------------------------------

void OverlayWindowImpl::PostState(const ControllerState& state) {
    const int next = 1 - m_activeBuffer;
    m_stateBuffers[next] = state;
    // Release-store: render thread reads with acquire
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
// Window + D2D setup
// ---------------------------------------------------------------------------

void OverlayWindowImpl::CreateWindowAndD2D() {
    // Enumerate monitors to find the target one
    struct MonitorEnum {
        uint32_t target;
        uint32_t current = 0;
        HMONITOR result  = nullptr;
        RECT     rect    = {};
    } ctx{m_config.monitorIndex};

    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hmon, HDC, LPRECT rc, LPARAM lp) -> BOOL {
        auto& e = *reinterpret_cast<MonitorEnum*>(lp);
        if (e.current++ == e.target) { e.result = hmon; e.rect = *rc; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    if (!ctx.result) {
        // Fallback to primary
        ctx.rect = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
    }

    m_monitor = ctx.result;
    m_monitorRect = { ctx.rect.left, ctx.rect.top, ctx.rect.right, ctx.rect.bottom };

    // DPI
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

    m_hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"EnjoyStickOverlay", L"EnjoyStick",
        WS_POPUP,
        ctx.rect.left, ctx.rect.top,
        m_monitorRect.Width(), m_monitorRect.Height(),
        nullptr, nullptr, wc.hInstance, this);

    if (!m_hwnd) throw std::runtime_error("Failed to create overlay HWND");

    SetLayeredWindowAttributes(m_hwnd, 0, 255, LWA_ALPHA);
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);

    // Create D2D factory
    D2D1_FACTORY_OPTIONS opts{};
#ifdef _DEBUG
    opts.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
    D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,
                      __uuidof(ID2D1Factory1), &opts,
                      reinterpret_cast<void**>(m_d2dFactory.GetAddressOf()));

    // DWrite
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                        __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()));
}

void OverlayWindowImpl::DestroyWindowAndD2D() {
    m_d2dContext.Reset();
    m_d2dFactory.Reset();
    m_dwriteFactory.Reset();
    m_dcompVisual.Reset();
    m_dcompTarget.Reset();
    m_dcompDevice.Reset();
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
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
    const float targetMs = 1000.0f / static_cast<float>(m_config.renderHz);
    using Clock = std::chrono::high_resolution_clock;
    auto prev   = Clock::now();

    while (m_running.load(std::memory_order_relaxed)) {
        const auto now   = Clock::now();
        const float dt   = std::chrono::duration<float, std::milli>(now - prev).count();
        prev             = now;

        // Consume pending state
        if (auto* p = m_pendingState.exchange(nullptr, std::memory_order_acquire)) {
            m_lastState    = *p;
            m_activeBuffer = 1 - m_activeBuffer;
        }

        // Consume pending toasts
        {
            std::lock_guard lock(m_toastMutex);
            while (!m_pendingToasts.empty()) {
                m_activeToasts.push_back(std::move(m_pendingToasts.front()));
                m_pendingToasts.pop();
            }
        }

        RenderFrame(dt * 0.001f);

        // Cap to target rate
        const float elapsed = std::chrono::duration<float, std::milli>(
            Clock::now() - prev).count();
        if (elapsed < targetMs) {
            const auto sleepMs = static_cast<DWORD>(targetMs - elapsed);
            if (sleepMs > 0) Sleep(sleepMs);
        }
    }
}

void OverlayWindowImpl::RenderFrame(float deltaSeconds) {
    if (!m_hwnd) return;

    // Lightweight: use a GDI-compatible render target for the layered window
    // (full DirectComposition swap chain is Phase 2 polish)
    PAINTSTRUCT ps;
    HDC hdc = GetDC(m_hwnd);
    if (!hdc) return;

    RECT wr;
    GetClientRect(m_hwnd, &wr);
    const int w = wr.right, h = wr.bottom;
    if (w == 0 || h == 0) { ReleaseDC(m_hwnd, hdc); return; }

    // Create a D2D render target backed by a DC
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> rt;
    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    m_d2dFactory->CreateDCRenderTarget(&rtp, rt.GetAddressOf());
    if (!rt) { ReleaseDC(m_hwnd, hdc); return; }

    rt->BindDC(hdc, &wr);
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));  // Fully transparent

    m_radialMenu.Update(m_lastState, deltaSeconds);
    m_radialMenu.Draw(rt.Get(), m_dpiScale);

    DrawActiveIndicator(rt.Get());
    DrawToasts(rt.Get(), deltaSeconds);

    rt->EndDraw();
    ReleaseDC(m_hwnd, hdc);
}

void OverlayWindowImpl::DrawActiveIndicator(ID2D1DeviceContext* /*dc*/) {
    // Phase 2: draw a small glowing dot in the bottom-right corner
    // indicating active controller + current mode (cursor / keyboard / gamepad)
}

void OverlayWindowImpl::DrawToasts(ID2D1DeviceContext* /*dc*/, float deltaSeconds) {
    auto& toasts = m_activeToasts;
    for (auto& t : toasts) t.elapsed += deltaSeconds * 1000.0f;
    toasts.erase(
        std::remove_if(toasts.begin(), toasts.end(),
            [](const ToastNotification& t) {
                return t.elapsed >= static_cast<float>(t.durationMs);
            }),
        toasts.end());
    // Phase 2: render slide-in / fade-out toast cards with DWrite text
}

} // namespace enjoystick::overlay
