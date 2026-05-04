#include <enjoystick/overlay/SettingsMenu.hpp>
#include "Overlay_Theme.hpp"

#include <Windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif

namespace enjoystick::overlay {

SettingsMenu::SettingsMenu(OnChangedCallback onChange)
    : m_onChange(std::move(onChange))
{ BuildRows(); }

void SettingsMenu::BuildRows() {
    m_rows.clear();
    m_rows.reserve(12);
    m_rows.push_back({ L"Cursor speed",            RowType::FloatSlider, 1.0f,   20.0f,  0.5f,  &m_values.cursorSpeed,       nullptr,                    L" px/ms" });
    m_rows.push_back({ L"Curve exponent",           RowType::FloatSlider, 0.8f,   3.0f,   0.05f, &m_values.curveExponent,     nullptr,                    L""       });
    m_rows.push_back({ L"Acceleration ramp",        RowType::FloatSlider, 20.0f,  500.0f, 10.0f, &m_values.accelerationMs,    nullptr,                    L" ms"    });
    m_rows.push_back({ L"Adaptive speed",           RowType::BoolToggle,  0,      0,      0,     nullptr,                     &m_values.adaptiveSpeed,    L""       });
    m_rows.push_back({ L"Traversal time",           RowType::FloatSlider, 300.0f, 2000.f, 50.0f, &m_values.targetTraversalMs, nullptr,                    L" ms"    });
    m_rows.push_back({ L"DPI weight",               RowType::FloatSlider, 0.0f,   1.0f,   0.05f, &m_values.dpiWeight,         nullptr,                    L""       });
    m_rows.push_back({ L"Scroll speed",             RowType::FloatSlider, 1.0f,   40.0f,  0.5f,  &m_values.scrollSpeed,       nullptr,                    L""       });
    m_rows.push_back({ L"Deadzone inner",           RowType::FloatSlider, 0.02f,  0.40f,  0.01f, &m_values.dzInner,           nullptr,                    L""       });
    m_rows.push_back({ L"Deadzone outer",           RowType::FloatSlider, 0.50f,  1.00f,  0.01f, &m_values.dzOuter,           nullptr,                    L""       });
    m_rows.push_back({ L"Triggers as clicks",       RowType::BoolToggle,  0,      0,      0,     nullptr,                     &m_values.triggersAsClicks, L""       });
    m_rows.push_back({ L"Right stick moves cursor", RowType::BoolToggle,  0,      0,      0,     nullptr,                     &m_values.useRightStick,    L""       });
}

void SettingsMenu::Open(const Values& current) {
    m_values       = current;
    BuildRows();
    m_selectedRow  = 0;
    m_state        = State::Opening;
    m_animProgress = 0.0f;
    m_repeatTimer  = 0.0f;
}
void SettingsMenu::Close() {
    if (m_state == State::Hidden) return;
    m_state = State::Closing;
}
bool SettingsMenu::IsOpen() const noexcept { return m_state != State::Hidden; }

void SettingsMenu::Update(const ControllerState& state, float deltaSeconds) {
    UpdateAnimation(deltaSeconds);
    if (m_state != State::Visible && m_state != State::Opening) return;

    const bool south  = HasButton(state.buttons, Button::South);
    const bool east   = HasButton(state.buttons, Button::East);
    const bool dUp    = HasButton(state.buttons, Button::DPadUp);
    const bool dDown  = HasButton(state.buttons, Button::DPadDown);
    const bool dLeft  = HasButton(state.buttons, Button::DPadLeft);
    const bool dRight = HasButton(state.buttons, Button::DPadRight);

    if (east && !m_prevEast) { Close(); goto done; }
    if (dUp   && !m_prevDUp)   { const int32_t n = static_cast<int32_t>(m_rows.size()); m_selectedRow = (m_selectedRow - 1 + n) % n; m_repeatTimer = 0.0f; }
    if (dDown && !m_prevDDown) { const int32_t n = static_cast<int32_t>(m_rows.size()); m_selectedRow = (m_selectedRow + 1)     % n; m_repeatTimer = 0.0f; }
    if (south && !m_prevSouth) {
        const auto& row = m_rows[static_cast<size_t>(m_selectedRow)];
        if (row.type == RowType::BoolToggle && row.bTarget) {
            *row.bTarget = !(*row.bTarget);
            CommitChange();
        }
    }
    {
        const bool hLeft  = dLeft;
        const bool hRight = dRight;
        if (hLeft  && !m_prevDLeft)  AdjustSelected(-1.0f, false);
        if (hRight && !m_prevDRight) AdjustSelected( 1.0f, false);
        if (hLeft || hRight) {
            m_repeatTimer += deltaSeconds;
            if (m_repeatTimer >= 1.0f / kRepeatHz) {
                m_repeatTimer -= 1.0f / kRepeatHz;
                if (hLeft)  AdjustSelected(-1.0f, true);
                if (hRight) AdjustSelected( 1.0f, true);
            }
        } else { m_repeatTimer = 0.0f; }
    }
    {
        const float lx = state.leftStick.x;
        if (std::abs(lx) > 0.25f) {
            const auto& row = m_rows[static_cast<size_t>(m_selectedRow)];
            if (row.type == RowType::FloatSlider && row.fTarget) {
                *row.fTarget = std::clamp(*row.fTarget + lx * deltaSeconds * row.step * 10.0f, row.min, row.max);
                CommitChange();
            }
        }
    }
done:
    m_prevSouth = south; m_prevEast  = east;
    m_prevDUp   = dUp;   m_prevDDown = dDown;
    m_prevDLeft = dLeft; m_prevDRight = dRight;
}

void SettingsMenu::UpdateAnimation(float deltaSeconds) {
    const float step = deltaSeconds * 1000.0f / kAnimMs;
    switch (m_state) {
    case State::Opening:
        m_animProgress = std::min(1.0f, m_animProgress + step);
        if (m_animProgress >= 1.0f) m_state = State::Visible;
        break;
    case State::Closing:
        m_animProgress = std::max(0.0f, m_animProgress - step);
        if (m_animProgress <= 0.0f) m_state = State::Hidden;
        break;
    default: break;
    }
}
void SettingsMenu::AdjustSelected(float direction, bool repeat) {
    const auto& row = m_rows[static_cast<size_t>(m_selectedRow)];
    if (row.type != RowType::FloatSlider || !row.fTarget) return;
    const float step = repeat ? row.step * 2.0f : row.step;
    *row.fTarget = std::clamp(*row.fTarget + direction * step, row.min, row.max);
    CommitChange();
}
void SettingsMenu::CommitChange() { if (m_onChange) m_onChange(m_values); }

// ---------------------------------------------------------------------------
// Internal geometry helpers
// ---------------------------------------------------------------------------
namespace {

static void DrawPanelChrome(
    ID2D1RenderTarget* rt, ID2D1Factory* fac,
    float px, float py, float pw, float ph, float r, float s, float ease) noexcept
{
    // Outer glow border
    {
        D2D1_ROUNDED_RECT rr{ D2D1::RectF(px, py, px+pw, py+ph), r, r };
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldShadow(0.30f * ease), b.GetAddressOf());
        if (b) rt->DrawRoundedRectangle(rr, b.Get(), 1.8f);
    }
    // Inner hairline border
    {
        const float d = 0.5f;
        D2D1_ROUNDED_RECT rr{ D2D1::RectF(px+d, py+d, px+pw-d, py+ph-d), r-d, r-d };
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::InkLine(0.88f * ease), b.GetAddressOf());
        if (b) rt->DrawRoundedRectangle(rr, b.Get(), 0.65f);
    }
    // Top luminance strip
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sp;
        rt->CreateSolidColorBrush(Tok::White(0.062f * ease), sp.GetAddressOf());
        if (sp) rt->DrawLine(
            D2D1::Point2F(px + r,          py + 1.3f * s),
            D2D1::Point2F(px + pw * 0.44f, py + 1.3f * s),
            sp.Get(), 0.9f);
    }
    // Left gold accent column
    {
        const float aw = 4.0f * s;
        D2D1_ROUNDED_RECT ar{ D2D1::RectF(px, py+r, px+aw, py+ph-r), aw*0.5f, aw*0.5f };
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldDeep(0.82f * ease), b.GetAddressOf());
        if (b) rt->FillRoundedRectangle(ar, b.Get());
    }
    (void)fac;
}

static void DrawProgressBar(
    ID2D1RenderTarget* rt,
    float bx0, float by, float bx1, float barH, float barR,
    float frac, bool sel, float ease) noexcept
{
    // Track
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceRaised(0.95f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT tr{ D2D1::RectF(bx0, by, bx1, by+barH), barR, barR };
            rt->FillRoundedRectangle(tr, b.Get()); }
    }
    // Track border
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::InkLine(0.92f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT tr{ D2D1::RectF(bx0, by, bx1, by+barH), barR, barR };
            rt->DrawRoundedRectangle(tr, b.Get(), 0.7f); }
    }
    if (frac <= 0.005f) return;
    const float bxFill = bx0 + (bx1 - bx0) * frac;
    // Fill
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(
            sel ? Tok::GoldWarm(0.94f * ease) : Tok::GoldDeep(0.68f * ease),
            b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT fr{ D2D1::RectF(bx0, by, bxFill, by+barH), barR, barR };
            rt->FillRoundedRectangle(fr, b.Get()); }
    }
    // Knob
    {
        const float kr = barH * 1.3f;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(
            sel ? Tok::GoldBright(0.99f * ease) : Tok::GoldMid(0.62f * ease),
            b.GetAddressOf());
        if (b) rt->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(bxFill, by + barH * 0.5f), kr, kr),
            b.Get());
    }
    // Knob border ring
    if (sel) {
        const float kr = barH * 1.3f;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldAccent(0.74f * ease), b.GetAddressOf());
        if (b) rt->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(bxFill, by + barH * 0.5f), kr + 1.8f, kr + 1.8f),
            b.Get(), 1.0f);
    }
}

} // anon

// ---------------------------------------------------------------------------
// Draw — Futurist Glamour v3, TV-scale
//
// Key sizing changes vs previous:
//   Panel width:   700  →  860 px
//   Row height:     56  →   80 px
//   Header height:  76  →  100 px
//   Footer height:  36  →   48 px
//   Body font:    13.5  →   17 px
//   Title font:   16.5  →   22 px
//   Value font:   13.5  →   17 px
//   Hint font:    10.5  →   13 px
//   Progress bar:    6  →    9 px
//   Left accent:     3  →    4 px
// ---------------------------------------------------------------------------
void SettingsMenu::Draw(
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
    const float e    = m_animProgress;
    const float ease = 1.0f - std::pow(1.0f - e, 3.0f);

    // ---- TV-scale layout constants -----------------------------------------
    const float kPanelW  = 860.0f * s;
    const float kRowH    =  80.0f * s;
    const float kHeaderH = 100.0f * s;
    const float kFooterH =  48.0f * s;
    const float kPadX    =  32.0f * s;
    const float kPadY    =  18.0f * s;
    const float kRadius  =  16.0f * s;
    const float kBarH    =   9.0f * s;
    const float kBarR    =   4.5f * s;
    const float kAccentW =   4.0f * s;
    const float kFBody   =  17.0f * s;
    const float kFTitle  =  22.0f * s;
    const float kFHint   =  13.0f * s;
    const float kFVal    =  17.0f * s;

    const int32_t rowCount = static_cast<int32_t>(m_rows.size());
    const float   kPanelH  = std::min(
        kHeaderH + kRowH * static_cast<float>(rowCount) + kPadY * 2.0f + kFooterH,
        screenH * 0.92f);
    const float panelY  = (screenH - kPanelH) * 0.5f;
    const float targetX = screenW - kPanelW - 36.0f * s;
    const float panelX  = screenW - ease * (screenW - targetX);

    Microsoft::WRL::ComPtr<ID2D1Factory> fac;
    rt->GetFactory(fac.GetAddressOf());

    // ---- Scrim
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::Scrim(0.54f * ease), b.GetAddressOf());
        if (b) rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, screenW, screenH), b.Get());
    }

    // ---- Panel glass fill
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceGlass(0.97f * ease), b.GetAddressOf());
        if (b) {
            D2D1_ROUNDED_RECT rr{ D2D1::RectF(panelX, panelY, panelX+kPanelW, panelY+kPanelH), kRadius, kRadius };
            rt->FillRoundedRectangle(rr, b.Get());
        }
    }
    DrawPanelChrome(rt, fac.Get(), panelX, panelY, kPanelW, kPanelH, kRadius, s, ease);

    // ---- Header -------------------------------------------------------------
    {
        const float bw = 54.0f * s, bh = 54.0f * s;
        const float bx = panelX + kPadX + kAccentW + 12.0f * s;
        const float by = panelY + (kHeaderH - bh) * 0.5f;
        // App icon badge
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(Tok::GoldDeep(0.62f * ease), b.GetAddressOf());
            if (b) { D2D1_ROUNDED_RECT rr{ D2D1::RectF(bx,by,bx+bw,by+bh), 13.0f*s, 13.0f*s };
                rt->FillRoundedRectangle(rr, b.Get()); }
        }
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(Tok::GoldWarm(0.58f * ease), b.GetAddressOf());
            if (b) { D2D1_ROUNDED_RECT rr{ D2D1::RectF(bx,by,bx+bw,by+bh), 13.0f*s, 13.0f*s };
                rt->DrawRoundedRectangle(rr, b.Get(), 1.2f); }
        }
        if (dwrite) {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> fi;
            dwrite->CreateTextFormat(L"Segoe UI Emoji", nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                24.0f*s, L"en-us", fi.GetAddressOf());
            if (fi) {
                fi->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                fi->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ib;
                rt->CreateSolidColorBrush(Tok::GoldHi(0.92f * ease), ib.GetAddressOf());
                if (ib) rt->DrawText(L"\u2699", 1, fi.Get(), D2D1::RectF(bx,by,bx+bw,by+bh), ib.Get());
            }
        }
        // Title text
        if (dwrite) {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> ft;
            dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                kFTitle, L"en-us", ft.GetAddressOf());
            if (ft) {
                ft->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tb;
                rt->CreateSolidColorBrush(Tok::GoldHi(0.97f * ease), tb.GetAddressOf());
                if (tb) rt->DrawText(L"Settings", 8, ft.Get(),
                    D2D1::RectF(bx + bw + 16.0f*s, panelY,
                                panelX + kPanelW * 0.60f, panelY + kHeaderH), tb.Get());
            }
            // Subtitle
            Microsoft::WRL::ComPtr<IDWriteTextFormat> fs;
            dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                kFHint, L"en-us", fs.GetAddressOf());
            if (fs) {
                fs->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sb;
                rt->CreateSolidColorBrush(Tok::ChromeMute(0.64f * ease), sb.GetAddressOf());
                if (sb) rt->DrawText(L"EnjoyStick Controller Overlay", 29, fs.Get(),
                    D2D1::RectF(bx + bw + 16.0f*s, panelY,
                                panelX + kPanelW * 0.60f, panelY + kHeaderH - 10.0f*s), sb.Get());
            }
            // Close hint
            Microsoft::WRL::ComPtr<IDWriteTextFormat> fh;
            dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                kFHint, L"en-us", fh.GetAddressOf());
            if (fh) {
                fh->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                fh->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hb;
                rt->CreateSolidColorBrush(Tok::ChromeMute(0.58f * ease), hb.GetAddressOf());
                if (hb) rt->DrawText(L"(B) Close", 9, fh.Get(),
                    D2D1::RectF(panelX + kPanelW * 0.60f, panelY,
                                panelX + kPanelW - kPadX, panelY + kHeaderH), hb.Get());
            }
        }
        // Header separator
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sep;
            rt->CreateSolidColorBrush(Tok::GoldDeep(0.52f * ease), sep.GetAddressOf());
            if (sep) rt->DrawLine(
                D2D1::Point2F(panelX + kPadX + kAccentW + 5.0f*s, panelY + kHeaderH - 0.5f),
                D2D1::Point2F(panelX + kPanelW - kPadX,             panelY + kHeaderH - 0.5f),
                sep.Get(), 0.9f);
        }
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sep;
            rt->CreateSolidColorBrush(Tok::GoldShadow(0.26f * ease), sep.GetAddressOf());
            if (sep) rt->DrawLine(
                D2D1::Point2F(panelX + kPadX + kAccentW + 5.0f*s, panelY + kHeaderH + 0.5f),
                D2D1::Point2F(panelX + kPanelW - kPadX,             panelY + kHeaderH + 0.5f),
                sep.Get(), 0.5f);
        }
    }

    // ---- Rows ---------------------------------------------------------------
    if (!dwrite) return;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtRow, fmtVal;
    dwrite->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        kFBody, L"en-us", fmtRow.GetAddressOf());
    dwrite->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        kFVal, L"en-us", fmtVal.GetAddressOf());
    if (!fmtRow || !fmtVal) return;
    fmtRow->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmtVal->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    fmtVal->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    const float rowsTop = panelY + kHeaderH + 1.0f;
    const float rowsBot = panelY + kPanelH - kFooterH - kPadY;
    const float labelR  = panelX + kPanelW * 0.54f;
    const float valueL  = panelX + kPanelW * 0.54f + 5.0f * s;
    const float valueR  = panelX + kPanelW - kPadX;

    for (int32_t i = 0; i < rowCount; ++i) {
        const float ry = rowsTop + static_cast<float>(i) * kRowH;
        if (ry + kRowH > rowsBot) break;

        const auto& row = m_rows[static_cast<size_t>(i)];
        const bool  sel = (i == m_selectedRow);

        // Selected row highlight pill
        if (sel) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> rb;
            rt->CreateSolidColorBrush(Tok::SurfaceRaised(0.74f * ease), rb.GetAddressOf());
            if (rb) {
                D2D1_ROUNDED_RECT rr{ D2D1::RectF(
                    panelX + kAccentW + 7.0f*s, ry + 2.5f*s,
                    panelX + kPanelW - 7.0f*s,  ry + kRowH - 2.5f*s), 10.0f*s, 10.0f*s };
                rt->FillRoundedRectangle(rr, rb.Get());
            }
            // Gold left accent bar
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ab;
                rt->CreateSolidColorBrush(Tok::GoldWarm(0.99f * ease), ab.GetAddressOf());
                if (ab) {
                    D2D1_ROUNDED_RECT ar{ D2D1::RectF(
                        panelX + kAccentW + 7.0f*s, ry + 10.0f*s,
                        panelX + kAccentW + 7.0f*s + kAccentW, ry + kRowH - 10.0f*s),
                        kAccentW*0.5f, kAccentW*0.5f };
                    rt->FillRoundedRectangle(ar, ab.Get());
                }
            }
            // Outer border on selected pill
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ab;
                rt->CreateSolidColorBrush(Tok::GoldShadow(0.38f * ease), ab.GetAddressOf());
                if (ab) {
                    D2D1_ROUNDED_RECT rr{ D2D1::RectF(
                        panelX + kAccentW + 7.0f*s, ry + 2.5f*s,
                        panelX + kPanelW - 7.0f*s,  ry + kRowH - 2.5f*s), 10.0f*s, 10.0f*s };
                    rt->DrawRoundedRectangle(rr, ab.Get(), 0.9f);
                }
            }
        }

        // Label
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> lb;
            rt->CreateSolidColorBrush(
                sel ? Tok::ChromeHi(0.98f * ease) : Tok::ChromeMid(0.70f * ease),
                lb.GetAddressOf());
            const float labelX = panelX + kPadX + kAccentW * 2.0f + 12.0f * s;
            if (lb) rt->DrawText(row.label, static_cast<UINT32>(std::wcslen(row.label)),
                fmtRow.Get(), D2D1::RectF(labelX, ry, labelR, ry + kRowH), lb.Get());
        }

        // Value / Control
        if (row.type == RowType::BoolToggle && row.bTarget) {
            const bool  on    = *row.bTarget;
            const float pillW = 66.0f * s;
            const float pillH = 34.0f * s;
            const float px0   = valueR - pillW;
            const float py0   = ry + (kRowH - pillH) * 0.5f;
            const float pr    = pillH * 0.5f;
            // Pill fill
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pb;
                rt->CreateSolidColorBrush(
                    on ? Tok::GoldWarm(0.92f * ease) : Tok::InkLine(0.87f * ease),
                    pb.GetAddressOf());
                if (pb) { D2D1_ROUNDED_RECT pill{ D2D1::RectF(px0,py0,px0+pillW,py0+pillH), pr, pr };
                    rt->FillRoundedRectangle(pill, pb.Get()); }
            }
            // Pill border
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pb;
                rt->CreateSolidColorBrush(
                    on ? Tok::GoldHi(0.64f * ease) : Tok::ChromeMute(0.44f * ease),
                    pb.GetAddressOf());
                if (pb) { D2D1_ROUNDED_RECT pill{ D2D1::RectF(px0,py0,px0+pillW,py0+pillH), pr, pr };
                    rt->DrawRoundedRectangle(pill, pb.Get(), 0.9f); }
            }
            // Thumb
            {
                const float margin = pr * 0.85f;
                const float tx = on ? px0 + pillW - margin : px0 + margin;
                const float ty = py0 + pr;
                const float tr = pr * 0.56f;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tb;
                rt->CreateSolidColorBrush(
                    on ? Tok::ChromeHi(0.98f * ease) : Tok::ChromeMute(0.70f * ease),
                    tb.GetAddressOf());
                if (tb) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx, ty), tr, tr), tb.Get());
                // Thumb highlight specular
                if (on) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sp;
                    rt->CreateSolidColorBrush(Tok::White(0.32f * ease), sp.GetAddressOf());
                    if (sp) rt->FillEllipse(
                        D2D1::Ellipse(
                            D2D1::Point2F(tx - tr * 0.25f, ty - tr * 0.30f),
                            tr * 0.30f, tr * 0.22f),
                        sp.Get());
                }
            }
        } else {
            // Float: right-aligned value
            wchar_t valBuf[80] = {};
            if (row.fTarget) {
                if      (row.step < 0.05f) std::swprintf(valBuf, 80, L"%.2f%ls", *row.fTarget, row.unit);
                else if (row.step < 1.0f)  std::swprintf(valBuf, 80, L"%.1f%ls", *row.fTarget, row.unit);
                else                       std::swprintf(valBuf, 80, L"%.0f%ls", *row.fTarget, row.unit);
            }
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> vb;
            rt->CreateSolidColorBrush(
                sel ? Tok::GoldHi(0.97f * ease) : Tok::ChromeMid(0.64f * ease),
                vb.GetAddressOf());
            if (vb) rt->DrawText(valBuf, static_cast<UINT32>(std::wcslen(valBuf)),
                fmtVal.Get(), D2D1::RectF(valueL, ry, valueR, ry + kRowH * 0.6f), vb.Get());
        }

        // Progress bar (float sliders)
        if (row.type == RowType::FloatSlider && row.fTarget) {
            const float frac = std::clamp(
                (*row.fTarget - row.min) / (row.max - row.min), 0.0f, 1.0f);
            const float bx0 = panelX + kPadX + kAccentW * 2.0f + 12.0f * s;
            const float bx1 = valueR - 100.0f * s;
            const float by  = ry + kRowH - kBarH - 9.0f * s;
            if (bx1 > bx0 + 24.0f * s)
                DrawProgressBar(rt, bx0, by, bx1, kBarH, kBarR, frac, sel, ease);
        }

        // Row divider (skip last)
        if (i < rowCount - 1) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> div;
            rt->CreateSolidColorBrush(Tok::InkLine(0.62f * ease), div.GetAddressOf());
            if (div) rt->DrawLine(
                D2D1::Point2F(panelX + kPadX + kAccentW + 5.0f*s, ry + kRowH - 0.5f),
                D2D1::Point2F(panelX + kPanelW - kPadX,             ry + kRowH - 0.5f),
                div.Get(), 0.6f);
        }
    }

    // ---- Footer hint bar ----------------------------------------------------
    if (dwrite) {
        const float fy = panelY + kPanelH - kFooterH;
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sep;
            rt->CreateSolidColorBrush(Tok::GoldDeep(0.42f * ease), sep.GetAddressOf());
            if (sep) rt->DrawLine(
                D2D1::Point2F(panelX + kPadX + kAccentW + 5.0f*s, fy + 0.5f),
                D2D1::Point2F(panelX + kPanelW - kPadX,             fy + 0.5f),
                sep.Get(), 0.8f);
        }
        Microsoft::WRL::ComPtr<IDWriteTextFormat> fh;
        dwrite->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            kFHint, L"en-us", fh.GetAddressOf());
        if (fh) {
            fh->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fh->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hb;
            rt->CreateSolidColorBrush(Tok::ChromeMute(0.58f * ease), hb.GetAddressOf());
            if (hb) rt->DrawText(
                L"\u25B2\u25BC  Navigate     \u25C4\u25BA  Adjust     (A) Toggle     (B) Close",
                42, fh.Get(),
                D2D1::RectF(panelX + kPadX, fy, panelX + kPanelW - kPadX, panelY + kPanelH),
                hb.Get());
        }
    }
}

} // namespace enjoystick::overlay
