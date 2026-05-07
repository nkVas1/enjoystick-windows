#include <enjoystick/overlay/ControlsOverlay.hpp>
#include "Overlay_Theme.hpp"
#include "Overlay_SpringAnim.hpp"

#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

#include <algorithm>
#include <cmath>
#include <cwchar>

#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif
static constexpr float kPi = static_cast<float>(M_PI);

namespace enjoystick::overlay {

static float EaseOutCubic(float t) noexcept {
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}
static float EaseInCubic(float t) noexcept { return t * t * t; }

// ---------------------------------------------------------------------------
// BuildSections
// ---------------------------------------------------------------------------
void ControlsOverlay::BuildSections() {
    m_sections.clear();
    m_sections.reserve(6);

    {
        Section s;
        s.icon  = L"\U0001F5B1";
        s.title = L"Cursor Mode";
        s.bindings = {
            { L"Left Stick",               L"Move mouse cursor" },
            { L"Right Stick",              L"Scroll (vertical)" },
            { L"\u25CF  (A)",             L"Left click" },
            { L"\u25CF  (A) hold",        L"Left button drag" },
            { L"\u25C6  (B) tap",         L"Right click" },
            { L"\u25A0  (X)",             L"Browser Back (XButton1)" },
            { L"\u25B2  (Y)",             L"Middle click" },
            { L"LB",                       L"Ctrl+Shift+Tab \u2014 prev browser tab" },
            { L"RB",                       L"Ctrl+Tab \u2014 next browser tab" },
            { L"LB + RB",                  L"Toggle Cursor / Navigate mode" },
            { L"L3 (L-Stick click)",       L"Double left click" },
            { L"R3 (R-Stick click)",       L"Open virtual keyboard" },
            { L"DPad \u2191 / \u2193",    L"Scroll up / down" },
            { L"DPad \u2190 / \u2192",    L"Alt+Left / Alt+Right (browser history)" },
            { L"Select + DPad \u2191\u2193", L"Fast scroll \u00D75" },
            { L"Select",                   L"Win+Tab (Task View)" },
            { L"Start",                    L"Open virtual keyboard" },
        };
        m_sections.push_back(std::move(s));
    }
    {
        Section s;
        s.icon  = L"\u2B06";
        s.title = L"Navigate";
        s.bindings = {
            { L"LB + RB",                  L"Toggle Cursor / Navigate mode" },
            { L"Left Stick",               L"Keyboard arrow keys" },
            { L"Right Stick",              L"Scroll" },
            { L"DPad \u2190 / \u2192",    L"Tab / Shift+Tab (focus cycle)" },
            { L"DPad \u2191 / \u2193",    L"Page Up / Page Down" },
            { L"\u25CF  (A)",             L"Enter / Confirm" },
            { L"\u25C6  (B)",             L"Escape / Back" },
            { L"\u25A0  (X)",             L"Backspace / Delete" },
            { L"\u25B2  (Y)",             L"Space" },
            { L"LB",                       L"Home" },
            { L"RB",                       L"End" },
            { L"Start",                    L"Open virtual keyboard" },
        };
        m_sections.push_back(std::move(s));
    }
    {
        Section s;
        s.icon  = L"\u25CE";
        s.title = L"Radial";
        s.bindings = {
            { L"Guide button",             L"Toggle radial menu open / close" },
            { L"Right Stick",              L"Point to sector" },
            { L"\u25CF  (A)",             L"Confirm selection" },
            { L"\u25C6  (B)",             L"Close menu" },
            { L"Hold sector 1 s",          L"Auto-select (dwell confirm)" },
            { L"\u25CE  Desktop",         L"Show desktop" },
            { L"\u25CE  Files",           L"Open File Explorer" },
            { L"\u25CE  Settings",        L"Open EnjoyStick settings" },
            { L"\u25CE  Keyboard",        L"Open virtual keyboard" },
            { L"\u25CE  Search",          L"Win+S (Windows Search)" },
            { L"\u25CE  Media",           L"Play / Pause media" },
            { L"\u25CE  Controls",        L"This controls reference" },
            { L"\u25CE  Exit",            L"Quit EnjoyStick" },
        };
        m_sections.push_back(std::move(s));
    }
    {
        Section s;
        s.icon  = L"\u2328";
        s.title = L"Keyboard";
        s.bindings = {
            { L"Left Stick / DPad",        L"Move cursor to key" },
            { L"Right Stick",              L"Proximity hover (nearest key)" },
            { L"\u25CF  (A)",             L"Type highlighted key; hold to repeat" },
            { L"\u25A0  (X)",             L"Backspace; hold to repeat with accel" },
            { L"\u25B2  (Y)",             L"Submit text and close" },
            { L"\u25C6  (B)",             L"Cancel and close" },
            { L"LB",                       L"Toggle SYM / CYR / ALPHA layer" },
            { L"RB",                       L"Return to ALPHA layer" },
            { L"L3 (L-Stick click)",       L"Toggle Caps Lock" },
            { L"Shift key",                L"One-shot uppercase next char" },
        };
        m_sections.push_back(std::move(s));
    }
    {
        Section s;
        s.icon  = L"\u2699";
        s.title = L"Settings";
        s.bindings = {
            { L"Guide + Start",            L"Toggle settings menu" },
            { L"Tray icon dbl-click",       L"Open settings menu" },
            { L"DPad \u2191 / \u2193",    L"Navigate rows" },
            { L"Left Stick Y",             L"Navigate rows (analogue)" },
            { L"DPad \u2190 / \u2192",    L"Decrease / Increase value" },
            { L"Left Stick X",             L"Fine-tune value (step cooldown)" },
            { L"\u25CF  (A)",             L"Toggle boolean setting" },
            { L"\u25B2  (Y)",             L"Reset all to defaults" },
            { L"\u25C6  (B)",             L"Close settings" },
        };
        m_sections.push_back(std::move(s));
    }
    {
        Section s;
        s.icon  = L"\u2731";
        s.title = L"Combos";
        s.bindings = {
            { L"Guide",                    L"Open / close radial menu" },
            { L"Guide + Start",            L"Open / close settings menu" },
            { L"Guide + LB",               L"Open virtual keyboard" },
            { L"LB + RB",                  L"Toggle Cursor \u21D4 Navigate mode" },
            { L"Start (cursor mode)",      L"Open virtual keyboard" },
            { L"R3 (cursor mode)",         L"Open virtual keyboard" },
            { L"Select (cursor mode)",     L"Win+Tab (Task View)" },
        };
        m_sections.push_back(std::move(s));
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void ControlsOverlay::Open() {
    if (m_state == State::Visible || m_state == State::Opening) return;
    BuildSections();
    m_sectionIdx  = 0;
    m_scrollOffset = 0;
    m_state       = State::Opening;
    m_stickActive = false;
    m_stickCooldown = 0.0f;
    m_scrollDpadHeld  = false;
    m_scrollDpadTimer = 0.0f;
    m_scrollRyActive  = false;
    m_scrollRyCooldown= 0.0f;

    m_openMask = Button::None;

    m_prevEast = m_prevDLeft = m_prevDRight =
    m_prevDUp  = m_prevDDown = false;

    m_panelSpring.stiffness = 320.0f;
    m_panelSpring.damping   = 26.0f;
    m_panelSpring.Snap(0.0f);
    m_panelSpring.SetTarget(1.0f);

    m_tabSpring.stiffness = 400.0f;
    m_tabSpring.damping   = 28.0f;
    m_tabSpring.Snap(0.0f);
    m_tabSpring.SetTarget(0.0f);
}
void ControlsOverlay::Close() {
    if (m_state == State::Hidden || m_state == State::Closing) return;
    m_state = State::Closing;
    m_panelSpring.SetTarget(0.0f);
}
bool ControlsOverlay::IsOpen() const noexcept { return m_state != State::Hidden; }

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
void ControlsOverlay::Update(const ControllerState& state, float dt) {
    m_panelSpring.Step(dt);
    const float pv = std::max(0.0f, std::min(1.0f, m_panelSpring.value));

    if (m_state == State::Opening && (m_panelSpring.IsSettled(0.01f) || pv >= 0.999f))
        m_state = State::Visible;
    if (m_state == State::Closing) {
        if (m_panelSpring.IsSettled(0.01f) || pv <= 0.001f) {
            m_state = State::Hidden;
            return;
        }
    }
    if (m_state == State::Hidden) return;

    m_tabSpring.Step(dt);

    if (m_openMask == Button::None) {
        m_openMask = state.buttons;
    }
    m_openMask = static_cast<Button>(
        static_cast<uint32_t>(m_openMask) &
        static_cast<uint32_t>(state.buttons));

    const uint32_t effectiveMask =
        static_cast<uint32_t>(state.buttons) &
        ~static_cast<uint32_t>(m_openMask);

    auto btn = [&](Button b) -> bool {
        return (effectiveMask & static_cast<uint32_t>(b)) != 0;
    };

    const bool east   = btn(Button::East);
    const bool dLeft  = btn(Button::DPadLeft);
    const bool dRight = btn(Button::DPadRight);
    const bool dUp    = btn(Button::DPadUp);
    const bool dDown  = btn(Button::DPadDown);

    if (east && !m_prevEast) {
        Close();
        goto done;
    }

    // ---- Section navigation (DPad Left / Right)
    if (dRight && !m_prevDRight) {
        const int32_t n = static_cast<int32_t>(m_sections.size());
        m_sectionIdx = (m_sectionIdx + 1) % n;
        m_tabSpring.SetTarget(static_cast<float>(m_sectionIdx));
        m_stickActive  = false;
        m_scrollOffset = 0;
        m_scrollDpadHeld = false;
        m_scrollRyActive = false;
    }
    if (dLeft && !m_prevDLeft) {
        const int32_t n = static_cast<int32_t>(m_sections.size());
        m_sectionIdx = (m_sectionIdx - 1 + n) % n;
        m_tabSpring.SetTarget(static_cast<float>(m_sectionIdx));
        m_stickActive  = false;
        m_scrollOffset = 0;
        m_scrollDpadHeld = false;
        m_scrollRyActive = false;
    }

    // ---- Section navigation (Left Stick X)
    {
        const float lx = state.leftStick.x;
        if (std::abs(lx) > 0.50f) {
            if (!m_stickActive) {
                m_stickActive   = true;
                m_stickCooldown = kStickFirst;
                const int32_t n = static_cast<int32_t>(m_sections.size());
                m_sectionIdx = (m_sectionIdx + (lx > 0 ? 1 : -1) + n) % n;
                m_tabSpring.SetTarget(static_cast<float>(m_sectionIdx));
                m_scrollOffset = 0;
            } else {
                m_stickCooldown -= dt;
                if (m_stickCooldown <= 0.0f) {
                    m_stickCooldown = kStickNext;
                    const int32_t n = static_cast<int32_t>(m_sections.size());
                    m_sectionIdx = (m_sectionIdx + (lx > 0 ? 1 : -1) + n) % n;
                    m_tabSpring.SetTarget(static_cast<float>(m_sectionIdx));
                    m_scrollOffset = 0;
                }
            }
        } else {
            m_stickActive   = false;
            m_stickCooldown = 0.0f;
        }
    }

    // ---- Scroll binding list (DPad Up / Down)
    {
        const bool dVert = dUp || dDown;
        if (dVert) {
            if (!m_scrollDpadHeld) {
                m_scrollDpadHeld  = true;
                m_scrollDpadTimer = kScrollFirst;
                if (dUp && m_scrollOffset > 0) --m_scrollOffset;
                if (dDown) {
                    const int32_t total = m_sectionIdx < static_cast<int32_t>(m_sections.size())
                        ? static_cast<int32_t>(m_sections[static_cast<size_t>(m_sectionIdx)].bindings.size()) : 0;
                    if (m_scrollOffset < total - 1) ++m_scrollOffset;
                }
            } else {
                m_scrollDpadTimer -= dt;
                if (m_scrollDpadTimer <= 0.0f) {
                    m_scrollDpadTimer = kScrollNext;
                    if (dUp && m_scrollOffset > 0) --m_scrollOffset;
                    if (dDown) {
                        const int32_t total = m_sectionIdx < static_cast<int32_t>(m_sections.size())
                            ? static_cast<int32_t>(m_sections[static_cast<size_t>(m_sectionIdx)].bindings.size()) : 0;
                        if (m_scrollOffset < total - 1) ++m_scrollOffset;
                    }
                }
            }
        } else {
            m_scrollDpadHeld  = false;
            m_scrollDpadTimer = 0.0f;
        }
    }

    // ---- Scroll binding list (Right Stick Y)
    {
        const float ry = state.rightStick.y;
        if (std::abs(ry) > kScrollRyDz) {
            if (!m_scrollRyActive) {
                m_scrollRyActive   = true;
                m_scrollRyCooldown = kScrollFirst;
                const int32_t total = m_sectionIdx < static_cast<int32_t>(m_sections.size())
                    ? static_cast<int32_t>(m_sections[static_cast<size_t>(m_sectionIdx)].bindings.size()) : 0;
                if (ry > 0 && m_scrollOffset > 0)       --m_scrollOffset;
                if (ry < 0 && m_scrollOffset < total-1) ++m_scrollOffset;
            } else {
                m_scrollRyCooldown -= dt;
                if (m_scrollRyCooldown <= 0.0f) {
                    m_scrollRyCooldown = kScrollNext;
                    const int32_t total = m_sectionIdx < static_cast<int32_t>(m_sections.size())
                        ? static_cast<int32_t>(m_sections[static_cast<size_t>(m_sectionIdx)].bindings.size()) : 0;
                    if (ry > 0 && m_scrollOffset > 0)       --m_scrollOffset;
                    if (ry < 0 && m_scrollOffset < total-1) ++m_scrollOffset;
                }
            }
        } else {
            m_scrollRyActive   = false;
            m_scrollRyCooldown = 0.0f;
        }
    }

done:
    m_prevEast   = east;
    m_prevDLeft  = dLeft;
    m_prevDRight = dRight;
    m_prevDUp    = dUp;
    m_prevDDown  = dDown;
}

// ---------------------------------------------------------------------------
// Draw helpers
// ---------------------------------------------------------------------------
namespace {

static void DrawChip(
    ID2D1RenderTarget*   rt,
    IDWriteFactory*      dwrite,
    float x, float y,
    float s,
    float ease,
    const wchar_t*       text,
    bool                 isKey,
    float                fontSize) noexcept
{
    if (!rt || !dwrite) return;
    const float fs = fontSize * s;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    dwrite->CreateTextFormat(L"Segoe UI", nullptr,
        isKey ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        fs, L"en-us", fmt.GetAddressOf());
    if (!fmt) return;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> lay;
    dwrite->CreateTextLayout(text, static_cast<UINT32>(std::wcslen(text)),
        fmt.Get(), 400.0f * s, fs + 10.0f * s, lay.GetAddressOf());
    if (!lay) return;
    DWRITE_TEXT_METRICS tm{};
    lay->GetMetrics(&tm);

    if (isKey) {
        const float pH = 8.0f * s, pV = 4.0f * s;
        const float cw = tm.width + pH * 2.0f;
        const float ch = tm.height + pV * 2.0f;
        { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
          rt->CreateSolidColorBrush(Tok::SurfaceSunken(0.92f * ease), b.GetAddressOf());
          if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(x, y, x+cw, y+ch), 5.0f*s, 5.0f*s};
                   rt->FillRoundedRectangle(rr, b.Get()); } }
        { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
          rt->CreateSolidColorBrush(Tok::GoldMid(0.70f * ease), b.GetAddressOf());
          if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(x+0.5f, y+0.5f, x+cw-0.5f, y+ch-0.5f), 5.0f*s, 5.0f*s};
                   rt->DrawRoundedRectangle(rr, b.Get(), 1.0f); } }
        { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
          rt->CreateSolidColorBrush(Tok::GoldHi(0.94f * ease), b.GetAddressOf());
          if (b) rt->DrawTextLayout(D2D1::Point2F(x + pH, y + pV), lay.Get(), b.Get()); }
    } else {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::ChromeMid(0.84f * ease), b.GetAddressOf());
        if (b) rt->DrawTextLayout(D2D1::Point2F(x, y), lay.Get(), b.Get());
    }
}

} // anon

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------
void ControlsOverlay::Draw(
    void*  renderTargetPtr,
    void*  dwriteFactoryPtr,
    float  dpiScale,
    float  screenW,
    float  screenH) const
{
    if (m_state == State::Hidden) return;
    const float pv = std::max(0.0f, std::min(1.0f, m_panelSpring.value));
    if (pv <= 0.001f) return;
    if (!renderTargetPtr || !dwriteFactoryPtr) return;

    auto* rt     = static_cast<ID2D1RenderTarget*>(renderTargetPtr);
    auto* dwrite = static_cast<IDWriteFactory*>(dwriteFactoryPtr);
    const float s    = dpiScale;
    const float ease = EaseOutCubic(pv);
    // EaseInCubic reserved for future close animation
    (void)static_cast<float(*)(float) noexcept>(&EaseInCubic);

    // ---- Scrim
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::Scrim(0.72f * pv), b.GetAddressOf());
        if (b) rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, screenW, screenH), b.Get());
    }

    const float panelW = std::min(screenW * 0.82f, 820.0f * s);
    const float panelH = std::min(screenH * 0.78f, 600.0f * s);
    const float panelX = (screenW - panelW) * 0.5f;
    const float panelY = (screenH - panelH) * 0.5f + (1.0f - ease) * 60.0f * s;
    const float cr     = 16.0f * s;

    // ---- Panel fill
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceBase(0.97f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(panelX, panelY, panelX+panelW, panelY+panelH), cr, cr};
                 rt->FillRoundedRectangle(rr, b.Get()); }
    }
    const float accH = 4.0f * s;
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldMid(0.90f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(panelX, panelY, panelX+panelW, panelY + accH + cr), cr, cr};
                 rt->FillRoundedRectangle(rr, b.Get()); }
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceBase(0.97f * ease), b.GetAddressOf());
        if (b) rt->FillRectangle(D2D1::RectF(panelX, panelY + accH, panelX+panelW, panelY + accH + cr), b.Get());
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceBase(0.97f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(panelX, panelY + accH, panelX+panelW, panelY+panelH), cr, cr};
                 rt->FillRoundedRectangle(rr, b.Get()); }
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldShadow(0.30f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(panelX+0.5f, panelY+0.5f, panelX+panelW-0.5f, panelY+panelH-0.5f), cr, cr};
                 rt->DrawRoundedRectangle(rr, b.Get(), 0.8f); }
    }

    const float hdrY  = panelY + accH + 14.0f * s;
    const float padX  = 28.0f * s;

    // ---- Page counter badge  (e.g. "2 / 6" top-right)
    if (dwrite && !m_sections.empty()) {
        wchar_t pageBuf[16];
        std::swprintf(pageBuf, 16, L"%d / %d",
            m_sectionIdx + 1, static_cast<int32_t>(m_sections.size()));
        Microsoft::WRL::ComPtr<IDWriteTextFormat> pf;
        dwrite->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 11.0f * s, L"en-us", pf.GetAddressOf());
        if (pf) {
            pf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            pf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            const float bW = 54.0f * s, bH = 20.0f * s;
            const float bX = panelX + panelW - padX - bW;
            const float bY = hdrY + 8.0f * s;
            { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
              rt->CreateSolidColorBrush(Tok::SurfaceRaised(0.80f * ease), b.GetAddressOf());
              if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(bX, bY, bX+bW, bY+bH), 5.0f*s, 5.0f*s};
                       rt->FillRoundedRectangle(rr, b.Get()); } }
            { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
              rt->CreateSolidColorBrush(Tok::GoldMid(0.60f * ease), b.GetAddressOf());
              if (b) rt->DrawText(pageBuf, static_cast<UINT32>(std::wcslen(pageBuf)),
                  pf.Get(), D2D1::RectF(bX, bY, bX+bW, bY+bH), b.Get()); }
        }
    }

    if (dwrite && m_sectionIdx < static_cast<int32_t>(m_sections.size())) {
        const auto& sec = m_sections[static_cast<size_t>(m_sectionIdx)];
        { Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
          dwrite->CreateTextFormat(L"Segoe UI Emoji", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
              28.0f * s, L"en-us", fmt.GetAddressOf());
          if (fmt) {
              fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
              Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
              rt->CreateSolidColorBrush(Tok::GoldHi(0.92f * ease), b.GetAddressOf());
              if (b) rt->DrawText(sec.icon.c_str(), static_cast<UINT32>(sec.icon.size()),
                  fmt.Get(), D2D1::RectF(panelX+padX, hdrY, panelX+padX+36.0f*s, hdrY+36.0f*s), b.Get());
          }
        }
        { Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
          dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
              22.0f * s, L"en-us", fmt.GetAddressOf());
          if (fmt) {
              fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
              Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
              rt->CreateSolidColorBrush(Tok::ChromeHi(0.97f * ease), b.GetAddressOf());
              if (b) rt->DrawText(sec.title.c_str(), static_cast<UINT32>(sec.title.size()),
                  fmt.Get(), D2D1::RectF(panelX+padX+42.0f*s, hdrY, panelX+panelW-padX, hdrY+36.0f*s), b.Get());
          }
        }
        { Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
          dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
              12.0f * s, L"en-us", fmt.GetAddressOf());
          if (fmt) {
              fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
              fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
              Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
              rt->CreateSolidColorBrush(Tok::ChromeMute(0.55f * ease), b.GetAddressOf());
              const wchar_t* hint = L"\u25C6  Close";
              if (b) rt->DrawText(hint, static_cast<UINT32>(std::wcslen(hint)),
                  fmt.Get(), D2D1::RectF(panelX+padX, hdrY, panelX+panelW-padX, hdrY+36.0f*s), b.Get());
          }
        }
    }

    const float divY = hdrY + 42.0f * s;
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldDeep(0.40f * ease), b.GetAddressOf());
        if (b) rt->DrawLine(
            D2D1::Point2F(panelX + padX, divY),
            D2D1::Point2F(panelX + panelW - padX, divY),
            b.Get(), 0.8f);
    }

    // ---- Bindings list
    if (m_sectionIdx < static_cast<int32_t>(m_sections.size())) {
        const auto& bindings = m_sections[static_cast<size_t>(m_sectionIdx)].bindings;
        const float listTop  = divY + 10.0f * s;
        const float rowH     = 32.0f * s;
        const float fntSize  = 13.5f;
        const float keyColW  = 230.0f * s;
        const float arrowW   = 26.0f  * s;
        const float actX     = panelX + padX + keyColW + arrowW;
        const float maxY     = panelY + panelH - 54.0f * s;
        const int32_t total  = static_cast<int32_t>(bindings.size());

        // Clamp scrollOffset so it never exceeds last visible item
        const int32_t safeOffset = std::min(m_scrollOffset, std::max(0, total - 1));

        const bool canScrollUp   = safeOffset > 0;
        const bool canScrollDown = (listTop + static_cast<float>(total - safeOffset) * rowH) > maxY;

        if (canScrollUp && dwrite) {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
            dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                10.0f * s, L"en-us", fmt.GetAddressOf());
            if (fmt) {
                fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(Tok::GoldMid(0.45f * ease), b.GetAddressOf());
                const wchar_t* ind = L"\u25B2  scroll";
                if (b) rt->DrawText(ind, static_cast<UINT32>(std::wcslen(ind)),
                    fmt.Get(), D2D1::RectF(panelX, listTop - 12.0f*s, panelX+panelW-padX, listTop+2.0f*s), b.Get());
            }
        }

        for (int32_t i = safeOffset; i < total; ++i) {
            const float ry = listTop + static_cast<float>(i - safeOffset) * rowH;
            if (ry + rowH > maxY) break;

            const auto& bind = bindings[static_cast<size_t>(i)];

            if ((i - safeOffset) % 2 == 0) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bg;
                rt->CreateSolidColorBrush(Tok::SurfaceRaised(0.28f * ease), bg.GetAddressOf());
                if (bg) { D2D1_ROUNDED_RECT rr{
                    D2D1::RectF(panelX+padX*0.5f, ry+1.0f*s, panelX+panelW-padX*0.5f, ry+rowH-1.0f*s),
                    4.0f*s, 4.0f*s};
                    rt->FillRoundedRectangle(rr, bg.Get()); }
            }

            const float chipH    = 22.0f * s;
            const float chipVOff = (rowH - chipH) * 0.5f;
            DrawChip(rt, dwrite, panelX + padX, ry + chipVOff, s, ease, bind.keys.c_str(), true, fntSize);

            if (dwrite) {
                Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
                dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                    12.0f * s, L"en-us", fmt.GetAddressOf());
                if (fmt) {
                    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ab;
                    rt->CreateSolidColorBrush(Tok::GoldShadow(0.55f * ease), ab.GetAddressOf());
                    const wchar_t* arr = L"\u2192";
                    if (ab) rt->DrawText(arr, 1, fmt.Get(),
                        D2D1::RectF(panelX+padX+keyColW, ry, panelX+padX+keyColW+arrowW, ry+rowH), ab.Get());
                }
            }

            DrawChip(rt, dwrite, actX, ry + (rowH - 13.5f * s - 4.0f * s) * 0.5f,
                s, ease, bind.action.c_str(), false, fntSize);
        }

        if (canScrollDown && dwrite) {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
            dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                10.0f * s, L"en-us", fmt.GetAddressOf());
            if (fmt) {
                fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(Tok::GoldMid(0.45f * ease), b.GetAddressOf());
                const wchar_t* ind = L"\u25BC  more";
                if (b) rt->DrawText(ind, static_cast<UINT32>(std::wcslen(ind)),
                    fmt.Get(), D2D1::RectF(panelX, maxY, panelX+panelW-padX, maxY+14.0f*s), b.Get());
            }
        }
    }

    // ---- Section tab bar
    {
        const float tabBarY  = panelY + panelH - 52.0f * s;
        const float tabBarH  = 52.0f * s;
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(Tok::GoldDeep(0.30f * ease), b.GetAddressOf());
            if (b) rt->DrawLine(
                D2D1::Point2F(panelX+padX,        tabBarY),
                D2D1::Point2F(panelX+panelW-padX, tabBarY),
                b.Get(), 0.6f);
        }

        const int32_t n    = static_cast<int32_t>(m_sections.size());
        const float   tabW = (panelW - padX * 2.0f) / static_cast<float>(n);
        const float   tabSprV = m_tabSpring.value;

        for (int32_t i = 0; i < n; ++i) {
            const float tx    = panelX + padX + static_cast<float>(i) * tabW;
            const bool  isSel = (i == m_sectionIdx);
            const float dist  = std::abs(static_cast<float>(i) - tabSprV);
            const float blend = std::max(0.0f, 1.0f - dist);

            if (dwrite && i < static_cast<int32_t>(m_sections.size())) {
                Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
                dwrite->CreateTextFormat(L"Segoe UI Emoji", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                    13.0f * s, L"en-us", fmt.GetAddressOf());
                if (fmt) {
                    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    const auto& icon  = m_sections[static_cast<size_t>(i)].icon;
                    const float alpha = isSel
                        ? (0.60f + 0.40f * blend) * ease
                        : 0.38f * ease;
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                    rt->CreateSolidColorBrush(
                        isSel ? Tok::GoldHi(alpha) : Tok::ChromeMute(alpha), b.GetAddressOf());
                    if (b) rt->DrawText(icon.c_str(), static_cast<UINT32>(icon.size()),
                        fmt.Get(), D2D1::RectF(tx, tabBarY+2.0f*s, tx+tabW, tabBarY+22.0f*s), b.Get());
                }
            }

            if (dwrite && i < static_cast<int32_t>(m_sections.size())) {
                Microsoft::WRL::ComPtr<IDWriteTextFormat> lf;
                dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                    isSel ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                    9.5f * s, L"en-us", lf.GetAddressOf());
                if (lf) {
                    lf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    lf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    const auto& title = m_sections[static_cast<size_t>(i)].title;
                    const float lalpha = isSel
                        ? (0.70f + 0.28f * blend) * ease
                        : 0.28f * ease;
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> lb;
                    rt->CreateSolidColorBrush(
                        isSel ? Tok::GoldHi(lalpha) : Tok::ChromeMute(lalpha), lb.GetAddressOf());
                    if (lb) rt->DrawText(title.c_str(), static_cast<UINT32>(title.size()),
                        lf.Get(), D2D1::RectF(tx, tabBarY+22.0f*s, tx+tabW, tabBarY+tabBarH-8.0f*s), lb.Get());
                }
            }

            if (isSel) {
                const float ulW = tabW * 0.45f + tabW * 0.40f * blend;
                const float ulX = tx + (tabW - ulW) * 0.5f;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(Tok::GoldHi(0.80f * ease), b.GetAddressOf());
                if (b) rt->DrawLine(
                    D2D1::Point2F(ulX, panelY + panelH - 5.0f * s),
                    D2D1::Point2F(ulX + ulW, panelY + panelH - 5.0f * s),
                    b.Get(), 2.5f * s);
            }
        }

        if (dwrite) {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
            dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                10.5f * s, L"en-us", fmt.GetAddressOf());
            if (fmt) {
                fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(Tok::ChromeMute(0.40f * ease), b.GetAddressOf());
                const wchar_t* nh = L"\u25C4 / \u25BA  switch  \u25B2\u25BC  scroll";
                if (b) rt->DrawText(nh, static_cast<UINT32>(std::wcslen(nh)),
                    fmt.Get(), D2D1::RectF(panelX+padX, tabBarY, panelX+panelW-padX-4.0f*s, tabBarY+tabBarH), b.Get());
            }
        }
    }
}

} // namespace enjoystick::overlay
