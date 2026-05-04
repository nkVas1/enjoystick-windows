#include <enjoystick/overlay/VirtualKeyboard.hpp>
#include "Overlay_Theme.hpp"

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
static constexpr float kPi  = static_cast<float>(M_PI);

namespace enjoystick::overlay {

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
void VirtualKeyboard::BuildLayout() {
    m_rows.clear();

    // Row 0 — digits + backspace
    m_rows.push_back({
        { L"1", L"!", L"\u00B1" },
        { L"2", L"@", L"\u00B2" },
        { L"3", L"#", L"\u00B3" },
        { L"4", L"$", L"\u00A3" },
        { L"5", L"%", L"\u20AC" },
        { L"6", L"^", L"\u00B6" },
        { L"7", L"&", L"\u2022" },
        { L"8", L"*", L"\u00D7" },
        { L"9", L"(", L"\u2018" },
        { L"0", L")", L"\u2019" },
        { L"\u232B", L"\u232B", L"\u232B", 1.5f, true },  // backspace
    });

    // Row 1 — QWERTY
    m_rows.push_back({
        { L"q", L"Q", L"-" },
        { L"w", L"W", L"_" },
        { L"e", L"E", L"=" },
        { L"r", L"R", L"+" },
        { L"t", L"T", L"[" },
        { L"y", L"Y", L"]" },
        { L"u", L"U", L"{" },
        { L"i", L"I", L"}" },
        { L"o", L"O", L"\\" },
        { L"p", L"P", L"|" },
    });

    // Row 2 — ASDF
    m_rows.push_back({
        { L"a", L"A", L"~" },
        { L"s", L"S", L"`" },
        { L"d", L"D", L"<" },
        { L"f", L"F", L">" },
        { L"g", L"G", L"," },
        { L"h", L"H", L"." },
        { L"j", L"J", L"\u2026" },
        { L"k", L"K", L"\u2014" },
        { L"l", L"L", L":" },
        { L"\u23CE", L"\u23CE", L"\u23CE", 1.5f, true },  // enter
    });

    // Row 3 — ZXCV + space
    m_rows.push_back({
        { L"\u21E7", L"\u21E7", L"\u21E7", 1.5f, true }, // shift
        { L"z", L"Z", L"/" },
        { L"x", L"X", L"?" },
        { L"c", L"C", L"\'" },
        { L"v", L"V", L"\"" },
        { L"b", L"B", L"\u00A0" },
        { L"n", L"N", L"\u2122" },
        { L"m", L"M", L"\u00AE" },
        { L".", L".", L"," },
        { L"\u2423", L"\u2423", L"\u2423", 4.0f, true }, // space
    });
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
int32_t VirtualKeyboard::RowKeyCount(int32_t row) const noexcept {
    if (row < 0 || row >= static_cast<int32_t>(m_rows.size())) return 0;
    return static_cast<int32_t>(m_rows[static_cast<size_t>(row)].size());
}

const VirtualKeyboard::Key* VirtualKeyboard::CurrentKey() const noexcept {
    if (m_row < 0 || m_row >= static_cast<int32_t>(m_rows.size())) return nullptr;
    const auto& row = m_rows[static_cast<size_t>(m_row)];
    if (m_col < 0 || m_col >= static_cast<int32_t>(row.size())) return nullptr;
    return &row[static_cast<size_t>(m_col)];
}

std::wstring VirtualKeyboard::KeyDisplay(const Key& k) const {
    if (m_layer == Layer::Sym)   return k.symLabel;
    if (m_shift || m_caps)       return k.shiftLabel;
    return k.label;
}

void VirtualKeyboard::NavigateTo(int32_t row, int32_t col) {
    const int32_t numRows = static_cast<int32_t>(m_rows.size());
    row = (row % numRows + numRows) % numRows;
    const int32_t n = RowKeyCount(row);
    col = (n > 0) ? (col % n + n) % n : 0;
    m_row = row; m_col = col;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void VirtualKeyboard::Open(const std::wstring& seed) {
    if (m_state == State::Visible || m_state == State::Opening) return;
    BuildLayout();
    m_text         = seed;
    m_row          = 3; // start on space row
    m_col          = 9; // space key
    m_layer        = Layer::Alpha;
    m_shift        = false;
    m_caps         = false;
    m_glowPhase    = 0.0f;
    m_animProgress = 0.0f;
    m_state        = State::Opening;
    m_stickCooldown = 0.0f;
    m_stickActive   = false;
    m_prevSouth = m_prevEast = m_prevWest = m_prevNorth =
    m_prevLB    = m_prevRB  = m_prevLS   = false;
}

void VirtualKeyboard::Close() {
    if (m_state == State::Hidden || m_state == State::Closing) return;
    m_state = State::Closing;
}

bool VirtualKeyboard::IsOpen() const noexcept {
    return m_state != State::Hidden;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
void VirtualKeyboard::Update(const ControllerState& state, float dt) {
    // --- Animation ---
    const float step = dt * 1000.0f / kAnimMs;
    if (m_state == State::Opening) {
        m_animProgress = std::min(1.0f, m_animProgress + step);
        if (m_animProgress >= 1.0f) m_state = State::Visible;
    } else if (m_state == State::Closing) {
        m_animProgress = std::max(0.0f, m_animProgress - step);
        if (m_animProgress <= 0.0f) { m_state = State::Hidden; return; }
    }
    if (m_state == State::Hidden) return;

    m_glowPhase += dt * 3.0f;
    if (m_glowPhase > 2.0f * kPi) m_glowPhase -= 2.0f * kPi;

    // --- Button edges ---
    const bool south  = HasButton(state.buttons, Button::South);
    const bool east   = HasButton(state.buttons, Button::East);
    const bool west   = HasButton(state.buttons, Button::West);
    const bool north  = HasButton(state.buttons, Button::North);
    const bool lb     = HasButton(state.buttons, Button::LB);
    const bool rb     = HasButton(state.buttons, Button::RB);
    const bool ls     = HasButton(state.buttons, Button::LS);

    // Close
    if (east && !m_prevEast) { Close(); goto done; }

    // Submit
    if (north && !m_prevNorth) {
        if (m_onSubmit) m_onSubmit(m_text);
        Close();
        goto done;
    }

    // Backspace
    if (west && !m_prevWest) {
        if (!m_text.empty()) m_text.pop_back();
        goto done;
    }

    // Caps Lock
    if (ls && !m_prevLS) {
        m_caps  = !m_caps;
        m_shift = false;
        goto done;
    }

    // Layer toggle
    if (lb && !m_prevLB) {
        m_layer = (m_layer == Layer::Alpha) ? Layer::Sym : Layer::Alpha;
        goto done;
    }
    if (rb && !m_prevRB) {
        m_layer = Layer::Alpha;
        goto done;
    }

    // Type key
    if (south && !m_prevSouth) {
        if (const Key* k = CurrentKey()) TypeKey(*k);
        goto done;
    }

    // Stick navigation
    {
        const float lx = state.leftStick.x;
        const float ly = state.leftStick.y;
        const float dz = 0.35f;
        const bool  hx = std::abs(lx) > dz;
        const bool  hy = std::abs(ly) > dz;

        if (hx || hy) {
            if (!m_stickActive) {
                // First movement
                m_stickActive   = true;
                m_stickCooldown = kStickRepeatFirst;
                if (hx) NavigateTo(m_row, m_col + (lx > 0 ? 1 : -1));
                else    NavigateTo(m_row + (ly > 0 ? 1 : -1), m_col);
            } else {
                m_stickCooldown -= dt;
                if (m_stickCooldown <= 0.0f) {
                    m_stickCooldown = kStickRepeatNext;
                    if (hx) NavigateTo(m_row, m_col + (lx > 0 ? 1 : -1));
                    else    NavigateTo(m_row + (ly > 0 ? 1 : -1), m_col);
                }
            }
        } else {
            m_stickActive   = false;
            m_stickCooldown = 0.0f;
        }
    }

done:
    m_prevSouth = south; m_prevEast  = east;
    m_prevWest  = west;  m_prevNorth = north;
    m_prevLB    = lb;    m_prevRB    = rb;
    m_prevLS    = ls;
}

void VirtualKeyboard::TypeKey(const Key& k) {
    const std::wstring disp = KeyDisplay(k);
    if (disp.empty()) return;

    if (k.isSpecial) {
        if (k.label == L"\u232B") {                 // backspace
            if (!m_text.empty()) m_text.pop_back();
            return;
        }
        if (k.label == L"\u23CE") {                 // enter
            m_text += L'\n';
            if (m_onChar) m_onChar(L'\n');
            return;
        }
        if (k.label == L"\u21E7") {                 // shift
            m_shift = !m_shift;
            return;
        }
        if (k.label == L"\u2423") {                 // space
            m_text += L' ';
            if (m_onChar) m_onChar(L' ');
            return;
        }
        return;
    }

    // Regular character
    if (disp.size() == 1) {
        m_text += disp[0];
        if (m_onChar) m_onChar(disp[0]);
    }
    // One-shot shift
    if (m_shift && !m_caps) m_shift = false;
}

// ---------------------------------------------------------------------------
// Draw helpers
// ---------------------------------------------------------------------------
namespace {

// Draw a top-edge specular arc stroke on a rounded rect keycap —
// replaces the old oval fill blob.
// aFrom / aTo in radians, arcR = inner radius of stroke path.
static void DrawSpecularArc(
    ID2D1RenderTarget*  rt,
    ID2D1Factory*       fac,
    float cx, float cy, float rx, float ry,
    float alpha)
{
    if (!rt || !fac || alpha < 0.004f) return;
    // Top arc from ~200deg to ~340deg (top of an ellipse)
    const float aFrom = -kPi * 0.88f;
    const float aTo   = -kPi * 0.12f;
    const float arcRx = rx - 1.5f;
    const float arcRy = ry - 1.5f;
    if (arcRx <= 0.0f || arcRy <= 0.0f) return;

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> geom;
    fac->CreatePathGeometry(geom.GetAddressOf());
    if (!geom) return;
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
    geom->Open(sink.GetAddressOf());
    if (!sink) return;

    sink->BeginFigure(
        D2D1::Point2F(cx + arcRx * std::cos(aFrom), cy + arcRy * std::sin(aFrom)),
        D2D1_FIGURE_BEGIN_HOLLOW);
    D2D1_ARC_SEGMENT arc{};
    arc.point         = D2D1::Point2F(cx + arcRx * std::cos(aTo), cy + arcRy * std::sin(aTo));
    arc.size          = D2D1::SizeF(arcRx, arcRy);
    arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
    arc.arcSize       = D2D1_ARC_SIZE_SMALL;
    sink->AddArc(arc);
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
    rt->CreateSolidColorBrush(Tok::White(alpha), b.GetAddressOf());
    if (b) rt->DrawGeometry(geom.Get(), b.Get(), 1.0f);
}

} // anon namespace

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------
void VirtualKeyboard::Draw(
    void*  renderTargetPtr,
    void*  dwriteFactoryPtr,
    float  dpiScale,
    float  screenW,
    float  screenH) const
{
    if (m_state == State::Hidden || m_animProgress <= 0.0f) return;
    if (!renderTargetPtr || !dwriteFactoryPtr) return;

    auto* rt     = static_cast<ID2D1RenderTarget*>(renderTargetPtr);
    auto* dwrite = static_cast<IDWriteFactory*>(dwriteFactoryPtr);
    const float s    = dpiScale;
    const float ease = 1.0f - std::pow(1.0f - m_animProgress, 3.0f);

    Microsoft::WRL::ComPtr<ID2D1Factory> fac;
    rt->GetFactory(fac.GetAddressOf());

    // ---- Layout constants -------------------------------------------------
    const float kKeyW    = 52.0f * s;
    const float kKeyH    = 50.0f * s;
    const float kGap     =  5.0f * s;
    const float kCorner  =  7.0f * s;
    const float kPanelPX = 28.0f * s;
    const float kPanelPY = 20.0f * s;

    // Calculate panel width from widest row
    float maxRowW = 0.0f;
    for (const auto& row : m_rows) {
        float rw = 0.0f;
        for (const auto& k : row) rw += k.widthMul * kKeyW + kGap;
        if (rw > maxRowW) maxRowW = rw;
    }
    const float kPanelW = maxRowW + kPanelPX * 2.0f - kGap;
    const float kPanelH = static_cast<float>(m_rows.size()) * (kKeyH + kGap)
                          + kPanelPY * 2.0f
                          + 44.0f * s  // text bar
                          + 8.0f  * s; // hint bar

    // Slide up from bottom
    const float targetY = screenH - kPanelH - 24.0f * s;
    const float panelY  = screenH - ease * (screenH - targetY);
    const float panelX  = (screenW - kPanelW) * 0.5f;

    // ---- Scrim ------------------------------------------------------------
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::Scrim(0.38f * ease), b.GetAddressOf());
        if (b) rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, screenW, screenH), b.Get());
    }

    // ---- Panel background -------------------------------------------------
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceSunken(0.97f * ease), b.GetAddressOf());
        if (b) {
            D2D1_ROUNDED_RECT rr{ D2D1::RectF(panelX, panelY,
                panelX+kPanelW, panelY+kPanelH), 14.0f*s, 14.0f*s };
            rt->FillRoundedRectangle(rr, b.Get());
        }
    }
    // Panel outer gold border
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldShadow(0.32f * ease), b.GetAddressOf());
        if (b) {
            D2D1_ROUNDED_RECT rr{ D2D1::RectF(panelX, panelY,
                panelX+kPanelW, panelY+kPanelH), 14.0f*s, 14.0f*s };
            rt->DrawRoundedRectangle(rr, b.Get(), 1.2f);
        }
    }
    // Panel inner hairline
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::InkLine(0.70f * ease), b.GetAddressOf());
        if (b) {
            D2D1_ROUNDED_RECT rr{ D2D1::RectF(panelX+0.5f, panelY+0.5f,
                panelX+kPanelW-0.5f, panelY+kPanelH-0.5f), 14.0f*s, 14.0f*s };
            rt->DrawRoundedRectangle(rr, b.Get(), 0.6f);
        }
    }
    // Top specular line (arc stroke, NOT oval fill)
    if (fac) {
        DrawSpecularArc(rt, fac.Get(),
            panelX + kPanelW * 0.5f, panelY + 2.0f * s,
            kPanelW * 0.25f, 6.0f * s,
            0.055f * ease);
    }

    // ---- Text preview bar -------------------------------------------------
    const float tbH  = 40.0f * s;
    const float tbX0 = panelX + kPanelPX;
    const float tbX1 = panelX + kPanelW - kPanelPX;
    const float tbY  = panelY + kPanelPY - 2.0f * s;
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceBase(0.90f * ease), b.GetAddressOf());
        if (b) {
            D2D1_ROUNDED_RECT rr{ D2D1::RectF(tbX0, tbY, tbX1, tbY+tbH), 6.0f*s, 6.0f*s };
            rt->FillRoundedRectangle(rr, b.Get());
        }
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldDeep(0.55f * ease), b.GetAddressOf());
        if (b) {
            D2D1_ROUNDED_RECT rr{ D2D1::RectF(tbX0, tbY, tbX1, tbY+tbH), 6.0f*s, 6.0f*s };
            rt->DrawRoundedRectangle(rr, b.Get(), 1.0f);
        }
    }
    // Cursor blink — gold vertical line at end of text
    {
        const float cx = tbX0 + 10.0f * s +
            std::min(m_text.size() * 9.0f * s, tbX1 - tbX0 - 18.0f * s);
        const float cy0 = tbY + 8.0f * s;
        const float cy1 = tbY + tbH - 8.0f * s;
        const float blink = 0.5f + 0.5f * std::sin(m_glowPhase * 2.0f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldHi(blink * ease), b.GetAddressOf());
        if (b) rt->DrawLine(D2D1::Point2F(cx, cy0), D2D1::Point2F(cx, cy1), b.Get(), 1.5f*s);
    }
    if (dwrite) {
        // Show tail of text if too long
        const std::wstring display = m_text.size() > 48
            ? L"\u2026" + m_text.substr(m_text.size() - 47)
            : m_text;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
        dwrite->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 15.0f * s, L"en-us", fmt.GetAddressOf());
        if (fmt) {
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(Tok::ChromeHi(0.90f * ease), b.GetAddressOf());
            if (b) rt->DrawText(display.c_str(), static_cast<UINT32>(display.size()),
                fmt.Get(), D2D1::RectF(tbX0+10.0f*s, tbY, tbX1-10.0f*s, tbY+tbH), b.Get());
        }
    }

    // ---- Hint bar ---------------------------------------------------------
    if (dwrite) {
        const wchar_t* hint =
            (m_layer == Layer::Sym)
            ? L"[RB] Alpha   [\u21E7] Shift   [X] \u232B   [Y] Submit   [B] Close"
            : L"[LB] Symbols   [L3] Caps   [X] \u232B   [Y] Submit   [B] Close";
        Microsoft::WRL::ComPtr<IDWriteTextFormat> hf;
        dwrite->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 10.5f * s, L"en-us", hf.GetAddressOf());
        if (hf) {
            hf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            hf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(Tok::ChromeMute(0.55f * ease), b.GetAddressOf());
            if (b) rt->DrawText(hint, static_cast<UINT32>(std::wcslen(hint)), hf.Get(),
                D2D1::RectF(panelX, panelY + kPanelH - 22.0f*s,
                            panelX + kPanelW, panelY + kPanelH - 2.0f*s), b.Get());
        }
        // Layer / Caps indicator
        if (m_layer == Layer::Sym || m_caps) {
            const wchar_t* badge = m_layer == Layer::Sym ? L"SYM" : L"CAPS";
            Microsoft::WRL::ComPtr<IDWriteTextFormat> bf;
            dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, 10.0f * s, L"en-us", bf.GetAddressOf());
            if (bf) {
                bf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                bf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                const float bw = 42.0f * s, bh = 18.0f * s;
                const float bx = panelX + kPanelW - kPanelPX - bw;
                const float by = panelY + kPanelPY + tbH - bh - 2.0f * s;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fill;
                rt->CreateSolidColorBrush(Tok::GoldDeep(0.75f * ease), fill.GetAddressOf());
                if (fill) { D2D1_ROUNDED_RECT rr{ D2D1::RectF(bx,by,bx+bw,by+bh), 4.0f*s, 4.0f*s };
                    rt->FillRoundedRectangle(rr, fill.Get()); }
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tb2;
                rt->CreateSolidColorBrush(Tok::GoldAccent(0.95f * ease), tb2.GetAddressOf());
                if (tb2) rt->DrawText(badge, static_cast<UINT32>(std::wcslen(badge)), bf.Get(),
                    D2D1::RectF(bx, by, bx+bw, by+bh), tb2.Get());
            }
        }
    }

    // ---- Keys -------------------------------------------------------------
    const float keysTop = panelY + kPanelPY + tbH + 6.0f * s;
    const float glowBreathe = 0.5f + 0.5f * std::sin(m_glowPhase);

    for (int32_t ri = 0; ri < static_cast<int32_t>(m_rows.size()); ++ri) {
        const auto& row = m_rows[static_cast<size_t>(ri)];
        const float ry  = keysTop + static_cast<float>(ri) * (kKeyH + kGap);

        // Calc total row width to centre
        float rowW = -kGap;
        for (const auto& k : row) rowW += k.widthMul * kKeyW + kGap;
        float rx = panelX + (kPanelW - rowW) * 0.5f;

        for (int32_t ci = 0; ci < static_cast<int32_t>(row.size()); ++ci) {
            const auto&  k     = row[static_cast<size_t>(ci)];
            const float  kw    = k.widthMul * kKeyW;
            const bool   sel   = (ri == m_row && ci == m_col);
            const float  kx    = rx;
            const float  kCx   = kx + kw * 0.5f;
            const float  kCy   = ry + kKeyH * 0.5f;

            // Glow halo (selected only) — drawn BEFORE fill, spread ellipse stroke
            if (sel && fac) {
                const float gr = kKeyH * 0.5f + 6.0f * s + 3.0f * s * glowBreathe;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> gb;
                rt->CreateSolidColorBrush(
                    Tok::GoldGlow((0.24f + 0.10f * glowBreathe) * ease), gb.GetAddressOf());
                if (gb) rt->DrawEllipse(
                    D2D1::Ellipse(D2D1::Point2F(kCx, kCy), gr, gr * 0.55f),
                    gb.Get(), 6.0f * s);
            }

            // Keycap outer fill — SurfaceRaised when selected, SurfaceBase otherwise
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(
                    sel ? Tok::SurfaceRaised(0.95f * ease)
                        : Tok::SurfaceBase(0.80f * ease),
                    b.GetAddressOf());
                if (b) {
                    D2D1_ROUNDED_RECT rr{ D2D1::RectF(kx, ry, kx+kw, ry+kKeyH),
                        kCorner, kCorner };
                    rt->FillRoundedRectangle(rr, b.Get());
                }
            }
            // Keycap inner sunken face (depth layer)
            {
                const float inset = 2.5f * s;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(
                    sel ? Tok::SurfaceBase(0.80f * ease)
                        : Tok::SurfaceSunken(0.70f * ease),
                    b.GetAddressOf());
                if (b) {
                    D2D1_ROUNDED_RECT rr{ D2D1::RectF(kx+inset, ry+inset,
                        kx+kw-inset, ry+kKeyH-inset*1.6f),
                        kCorner - inset, kCorner - inset };
                    rt->FillRoundedRectangle(rr, b.Get());
                }
            }
            // Keycap border
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(
                    sel ? Tok::GoldHi(0.92f * ease)
                        : Tok::InkLine(0.80f * ease),
                    b.GetAddressOf());
                if (b) {
                    D2D1_ROUNDED_RECT rr{ D2D1::RectF(kx, ry, kx+kw, ry+kKeyH),
                        kCorner, kCorner };
                    rt->DrawRoundedRectangle(rr, b.Get(), sel ? 1.6f : 0.7f);
                }
            }
            // Bevel rim (inset 1px, GoldShadow 25%) — skeuomorphic bottom shadow
            {
                const float bi = 1.0f;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(
                    sel ? Tok::GoldShadow(0.22f * ease)
                        : Tok::GoldDeep(0.15f * ease),
                    b.GetAddressOf());
                if (b) {
                    D2D1_ROUNDED_RECT rr{ D2D1::RectF(kx+bi, ry+bi, kx+kw-bi, ry+kKeyH-bi),
                        kCorner - bi, kCorner - bi };
                    rt->DrawRoundedRectangle(rr, b.Get(), 0.6f);
                }
            }
            // Top specular arc stroke (NOT an oval fill)
            if (fac) {
                DrawSpecularArc(rt, fac.Get(),
                    kCx, ry + kKeyH * 0.28f,
                    kw * 0.28f, kKeyH * 0.22f,
                    (sel ? 0.10f : 0.05f) * ease);
            }

            // Key label
            if (dwrite) {
                const std::wstring disp = KeyDisplay(k);
                if (!disp.empty()) {
                    const float fSize = k.isSpecial ? 13.0f * s : 15.0f * s;
                    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
                    dwrite->CreateTextFormat(
                        k.isSpecial ? L"Segoe UI Emoji" : L"Segoe UI",
                        nullptr,
                        k.isSpecial ? DWRITE_FONT_WEIGHT_NORMAL : DWRITE_FONT_WEIGHT_SEMI_BOLD,
                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                        fSize, L"en-us", fmt.GetAddressOf());
                    if (fmt) {
                        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tb;
                        rt->CreateSolidColorBrush(
                            sel   ? Tok::GoldAccent(0.98f * ease)
                            : k.isSpecial ? Tok::ChromeMid(0.65f * ease)
                            :               Tok::ChromeHi(0.78f * ease),
                            tb.GetAddressOf());
                        if (tb) rt->DrawText(disp.c_str(),
                            static_cast<UINT32>(disp.size()),
                            fmt.Get(),
                            D2D1::RectF(kx, ry, kx+kw, ry+kKeyH),
                            tb.Get());
                    }
                }
            }

            rx += kw + kGap;
        }
    }
}

} // namespace enjoystick::overlay
