#include <enjoystick/overlay/SettingsMenu.hpp>
#include "Overlay_Theme.hpp"

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

// ---------------------------------------------------------------------------
// BuildRows  - interleave SectionHeader sentinels between logical groups
// ---------------------------------------------------------------------------
void SettingsMenu::BuildRows() {
    m_rows.clear();
    m_rows.reserve(16);

    // ---- Section: Cursor
    m_rows.push_back({ L"Cursor",                  RowType::SectionHeader });
    m_rows.push_back({ L"Cursor speed",             RowType::FloatSlider, 1.0f,   20.0f,  0.5f,  &m_values.cursorSpeed,       nullptr,                    L" px/ms" });
    m_rows.push_back({ L"Curve exponent",           RowType::FloatSlider, 0.8f,   3.0f,   0.05f, &m_values.curveExponent,     nullptr,                    L""       });
    m_rows.push_back({ L"Acceleration ramp",        RowType::FloatSlider, 20.0f,  500.0f, 10.0f, &m_values.accelerationMs,    nullptr,                    L" ms"    });
    m_rows.push_back({ L"Right stick moves cursor", RowType::BoolToggle,  0,      0,      0,     nullptr,                     &m_values.useRightStick,    L""       });

    // ---- Section: Scrolling
    m_rows.push_back({ L"Scrolling",                RowType::SectionHeader });
    m_rows.push_back({ L"Scroll speed",             RowType::FloatSlider, 1.0f,   40.0f,  0.5f,  &m_values.scrollSpeed,       nullptr,                    L""       });
    m_rows.push_back({ L"Triggers as clicks",       RowType::BoolToggle,  0,      0,      0,     nullptr,                     &m_values.triggersAsClicks, L""       });

    // ---- Section: Adaptive Speed
    m_rows.push_back({ L"Adaptive Speed",           RowType::SectionHeader });
    m_rows.push_back({ L"Adaptive speed",           RowType::BoolToggle,  0,      0,      0,     nullptr,                     &m_values.adaptiveSpeed,    L""       });
    m_rows.push_back({ L"Traversal time",           RowType::FloatSlider, 300.0f, 2000.f, 50.0f, &m_values.targetTraversalMs, nullptr,                    L" ms"    });
    m_rows.push_back({ L"DPI weight",               RowType::FloatSlider, 0.0f,   1.0f,   0.05f, &m_values.dpiWeight,         nullptr,                    L""       });

    // ---- Section: Advanced
    m_rows.push_back({ L"Advanced",                 RowType::SectionHeader });
    m_rows.push_back({ L"Deadzone inner",           RowType::FloatSlider, 0.02f,  0.40f,  0.01f, &m_values.dzInner,           nullptr,                    L""       });
    m_rows.push_back({ L"Deadzone outer",           RowType::FloatSlider, 0.50f,  1.00f,  0.01f, &m_values.dzOuter,           nullptr,                    L""       });
}

bool SettingsMenu::IsInteractiveRow(int32_t idx) const noexcept {
    if (idx < 0 || idx >= static_cast<int32_t>(m_rows.size())) return false;
    return m_rows[static_cast<size_t>(idx)].type != RowType::SectionHeader;
}

int32_t SettingsMenu::NextInteractiveRow(int32_t from, int32_t dir) const noexcept {
    const int32_t n = static_cast<int32_t>(m_rows.size());
    if (n == 0) return from;
    int32_t idx = (from + dir + n) % n;
    for (int32_t guard = 0; guard < n; ++guard) {
        if (IsInteractiveRow(idx)) return idx;
        idx = (idx + dir + n) % n;
    }
    return from;
}

void SettingsMenu::Open(const Values& current) {
    m_values      = current;
    BuildRows();
    m_selectedRow = NextInteractiveRow(-1, 1);
    m_state       = State::Opening;
    m_animProgress = 0.0f;
    m_repeatTimer  = 0.0f;
    m_stickNavActive   = false;
    m_stickNavCooldown = 0.0f;
    m_prevSouth = m_prevEast = m_prevNorth =
    m_prevDUp = m_prevDDown = m_prevDLeft = m_prevDRight = false;
}
void SettingsMenu::Close() {
    if (m_state == State::Hidden) return;
    m_state = State::Closing;
}
bool SettingsMenu::IsOpen() const noexcept { return m_state != State::Hidden; }

void SettingsMenu::ResetToDefaults() {
    m_values = Values{};
    BuildRows();
    m_selectedRow = NextInteractiveRow(-1, 1);
    CommitChange();
}

void SettingsMenu::Update(const ControllerState& state, float deltaSeconds) {
    UpdateAnimation(deltaSeconds);
    if (m_state != State::Visible && m_state != State::Opening) return;

    const bool south  = HasButton(state.buttons, Button::South);
    const bool east   = HasButton(state.buttons, Button::East);
    const bool north  = HasButton(state.buttons, Button::North);
    const bool dUp    = HasButton(state.buttons, Button::DPadUp);
    const bool dDown  = HasButton(state.buttons, Button::DPadDown);
    const bool dLeft  = HasButton(state.buttons, Button::DPadLeft);
    const bool dRight = HasButton(state.buttons, Button::DPadRight);

    if (east  && !m_prevEast)  { Close(); goto done; }
    if (north && !m_prevNorth) { ResetToDefaults(); goto done; }

    // -------------------------------------------------------------------
    // DPad Up / Down — row navigation (edge-detect only, no auto-repeat
    // since DPad is digital and already sends repeated events via OS)
    // -------------------------------------------------------------------
    if (dUp   && !m_prevDUp)   { m_selectedRow = NextInteractiveRow(m_selectedRow, -1); m_repeatTimer = 0.0f; }
    if (dDown && !m_prevDDown) { m_selectedRow = NextInteractiveRow(m_selectedRow,  1); m_repeatTimer = 0.0f; }

    // -------------------------------------------------------------------
    // South button — toggle bool / confirm
    // -------------------------------------------------------------------
    if (south && !m_prevSouth) {
        const auto& row = m_rows[static_cast<size_t>(m_selectedRow)];
        if (row.type == RowType::BoolToggle && row.bTarget) {
            *row.bTarget = !(*row.bTarget);
            CommitChange();
        }
    }

    // -------------------------------------------------------------------
    // DPad Left / Right — slider adjustment with auto-repeat
    // -------------------------------------------------------------------
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

    // -------------------------------------------------------------------
    // Left stick — two-axis magnet-snap navigation
    //   Y axis: navigate rows with snap-cooldown (magnetic feel)
    //   X axis: fine-adjust slider continuously
    // -------------------------------------------------------------------
    {
        const float lx = state.leftStick.x;
        const float ly = state.leftStick.y;

        // Y-axis: vertical row navigation with magnet snap
        const bool stickUp   = (ly < -kNavDeadzone);
        const bool stickDown = (ly >  kNavDeadzone);
        if (stickUp || stickDown) {
            const int32_t dir = stickDown ? 1 : -1;
            if (!m_stickNavActive) {
                // First movement: snap immediately, then wait kSnapFirst
                m_stickNavActive   = true;
                m_stickNavCooldown = kSnapFirst;
                m_selectedRow      = NextInteractiveRow(m_selectedRow, dir);
                m_repeatTimer      = 0.0f;
            } else {
                m_stickNavCooldown -= deltaSeconds;
                if (m_stickNavCooldown <= 0.0f) {
                    // Subsequent snaps every kSnapNext
                    m_stickNavCooldown = kSnapNext;
                    m_selectedRow      = NextInteractiveRow(m_selectedRow, dir);
                    m_repeatTimer      = 0.0f;
                }
            }
        } else {
            // Stick returned to centre — reset snap state
            m_stickNavActive   = false;
            m_stickNavCooldown = 0.0f;
        }

        // X-axis: fine slider adjustment (continuous, analogue)
        if (std::abs(lx) > 0.25f) {
            const auto& row = m_rows[static_cast<size_t>(m_selectedRow)];
            if (row.type == RowType::FloatSlider && row.fTarget) {
                *row.fTarget = std::clamp(
                    *row.fTarget + lx * deltaSeconds * row.step * 10.0f,
                    row.min, row.max);
                CommitChange();
            }
        }
    }

done:
    m_prevSouth = south; m_prevEast  = east;  m_prevNorth = north;
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
    if (!IsInteractiveRow(m_selectedRow)) return;
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
    {
        D2D1_ROUNDED_RECT rr{ D2D1::RectF(px, py, px+pw, py+ph), r, r };
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldShadow(0.30f * ease), b.GetAddressOf());
        if (b) rt->DrawRoundedRectangle(rr, b.Get(), 1.8f);
    }
    {
        const float d = 0.5f;
        D2D1_ROUNDED_RECT rr{ D2D1::RectF(px+d, py+d, px+pw-d, py+ph-d), r-d, r-d };
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::InkLine(0.88f * ease), b.GetAddressOf());
        if (b) rt->DrawRoundedRectangle(rr, b.Get(), 0.65f);
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sp;
        rt->CreateSolidColorBrush(Tok::White(0.062f * ease), sp.GetAddressOf());
        if (sp) rt->DrawLine(
            D2D1::Point2F(px + r,          py + 1.3f * s),
            D2D1::Point2F(px + pw * 0.44f, py + 1.3f * s),
            sp.Get(), 0.9f);
    }
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
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceRaised(0.95f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT tr{ D2D1::RectF(bx0, by, bx1, by+barH), barR, barR };
            rt->FillRoundedRectangle(tr, b.Get()); }
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::InkLine(0.92f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT tr{ D2D1::RectF(bx0, by, bx1, by+barH), barR, barR };
            rt->DrawRoundedRectangle(tr, b.Get(), 0.7f); }
    }
    if (frac <= 0.005f) return;
    const float bxFill = bx0 + (bx1 - bx0) * frac;
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(
            sel ? Tok::GoldWarm(0.94f * ease) : Tok::GoldDeep(0.68f * ease),
            b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT fr{ D2D1::RectF(bx0, by, bxFill, by+barH), barR, barR };
            rt->FillRoundedRectangle(fr, b.Get()); }
    }
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
// Draw
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

    // ---- TV-scale layout constants
    const float kPanelW  = 860.0f * s;
    const float kRowH    =  80.0f * s;
    const float kSecH    =  44.0f * s;
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
    const float kFSec    =  11.0f * s;

    // Compute total rows height
    float totalRowsH = 0.0f;
    for (const auto& row : m_rows)
        totalRowsH += (row.type == RowType::SectionHeader) ? kSecH : kRowH;

    const float kPanelH = std::min(
        kHeaderH + totalRowsH + kPadY * 2.0f + kFooterH,
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

    // ---- Header
    {
        const float bw = 54.0f * s, bh = 54.0f * s;
        const float bx = panelX + kPadX + kAccentW + 12.0f * s;
        const float by = panelY + (kHeaderH - bh) * 0.5f;
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
            Microsoft::WRL::ComPtr<IDWriteTextFormat> fs;
            dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                kFHint, L"en-us", fs.GetAddressOf());
            if (fs) {
                fs->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sb;
                rt->CreateSolidColorBrush(Tok::ChromeMute(0.64f * ease), sb.GetAddressOf());
                if (sb) rt->DrawText(L"EnjoyStick  \u2022  Controller Settings",
                    static_cast<UINT32>(std::wcslen(L"EnjoyStick  \u2022  Controller Settings")),
                    fs.Get(),
                    D2D1::RectF(bx + bw + 16.0f*s, panelY,
                                panelX + kPanelW - kPadX, panelY + kHeaderH), sb.Get());
            }
        }
    }

    // ---- Header divider
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldDeep(0.42f * ease), b.GetAddressOf());
        if (b) rt->DrawLine(
            D2D1::Point2F(panelX + kPadX,            panelY + kHeaderH),
            D2D1::Point2F(panelX + kPanelW - kPadX,  panelY + kHeaderH),
            b.Get(), 0.8f * s);
    }

    // ---- Rows
    float rowY = panelY + kHeaderH + kPadY;
    for (int32_t ri = 0; ri < static_cast<int32_t>(m_rows.size()); ++ri) {
        const auto& row = m_rows[static_cast<size_t>(ri)];
        const bool  sel = (ri == m_selectedRow);

        if (row.type == RowType::SectionHeader) {
            // Section label
            if (dwrite) {
                Microsoft::WRL::ComPtr<IDWriteTextFormat> sf;
                dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                    DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                    kFSec, L"en-us", sf.GetAddressOf());
                if (sf) {
                    sf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sb;
                    rt->CreateSolidColorBrush(Tok::GoldMid(0.72f * ease), sb.GetAddressOf());
                    if (sb) rt->DrawText(row.label,
                        static_cast<UINT32>(std::wcslen(row.label)), sf.Get(),
                        D2D1::RectF(panelX + kPadX + kAccentW + 8.0f*s, rowY,
                                    panelX + kPanelW - kPadX, rowY + kSecH), sb.Get());
                }
            }
            // Section separator line
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(Tok::GoldDeep(0.22f * ease), b.GetAddressOf());
                if (b) rt->DrawLine(
                    D2D1::Point2F(panelX + kPadX + kAccentW + 8.0f*s, rowY + kSecH - 1.0f),
                    D2D1::Point2F(panelX + kPanelW - kPadX,            rowY + kSecH - 1.0f),
                    b.Get(), 0.6f);
            }
            rowY += kSecH;
            continue;
        }

        // ---- Selection highlight
        if (sel) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(Tok::SurfaceRaised(0.38f * ease), b.GetAddressOf());
            if (b) {
                D2D1_ROUNDED_RECT rr{ D2D1::RectF(
                    panelX + kAccentW + 2.0f*s, rowY + 2.0f*s,
                    panelX + kPanelW  - 2.0f*s, rowY + kRowH - 2.0f*s),
                    8.0f*s, 8.0f*s };
                rt->FillRoundedRectangle(rr, b.Get());
            }
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bb;
            rt->CreateSolidColorBrush(Tok::GoldMid(0.55f * ease), bb.GetAddressOf());
            if (bb) {
                D2D1_ROUNDED_RECT rr{ D2D1::RectF(
                    panelX + kAccentW + 2.0f*s, rowY + 2.0f*s,
                    panelX + kPanelW  - 2.0f*s, rowY + kRowH - 2.0f*s),
                    8.0f*s, 8.0f*s };
                rt->DrawRoundedRectangle(rr, bb.Get(), 0.8f);
            }
        }

        // ---- Row label
        if (dwrite && row.label) {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> lf;
            dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                kFBody, L"en-us", lf.GetAddressOf());
            if (lf) {
                lf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> lb;
                rt->CreateSolidColorBrush(
                    sel ? Tok::GoldHi(0.97f * ease) : Tok::ChromeMid(0.80f * ease),
                    lb.GetAddressOf());
                if (lb) rt->DrawText(row.label,
                    static_cast<UINT32>(std::wcslen(row.label)), lf.Get(),
                    D2D1::RectF(panelX + kPadX + kAccentW + 8.0f*s, rowY,
                                panelX + kPanelW * 0.55f, rowY + kRowH), lb.Get());
            }
        }

        // ---- Value / control
        const float ctrlX0 = panelX + kPanelW * 0.55f;
        const float ctrlX1 = panelX + kPanelW - kPadX;
        const float ctrlCY = rowY + kRowH * 0.5f;

        if (row.type == RowType::BoolToggle && row.bTarget) {
            const bool on = *row.bTarget;
            // Track
            const float tw = 52.0f * s, th = 26.0f * s;
            const float tx = ctrlX0 + (ctrlX1 - ctrlX0 - tw) * 0.5f;
            const float ty = ctrlCY - th * 0.5f;
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(
                    on ? Tok::GoldDeep(0.78f * ease) : Tok::SurfaceRaised(0.60f * ease),
                    b.GetAddressOf());
                if (b) { D2D1_ROUNDED_RECT rr{ D2D1::RectF(tx,ty,tx+tw,ty+th), th*0.5f,th*0.5f };
                    rt->FillRoundedRectangle(rr, b.Get()); }
            }
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(
                    on ? Tok::GoldMid(0.60f*ease) : Tok::InkLine(0.72f*ease), b.GetAddressOf());
                if (b) { D2D1_ROUNDED_RECT rr{ D2D1::RectF(tx,ty,tx+tw,ty+th), th*0.5f,th*0.5f };
                    rt->DrawRoundedRectangle(rr, b.Get(), 1.0f); }
            }
            // Thumb
            const float thumbX = on ? (tx + tw - th*0.5f) : (tx + th*0.5f);
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(
                    on ? Tok::GoldBright(0.99f*ease) : Tok::ChromeMute(0.60f*ease), b.GetAddressOf());
                if (b) rt->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(thumbX, ctrlCY), th*0.4f, th*0.4f), b.Get());
            }
        } else if (row.type == RowType::FloatSlider && row.fTarget) {
            const float frac = std::clamp((*row.fTarget - row.min) / (row.max - row.min), 0.0f, 1.0f);
            const float bx0  = ctrlX0 + 8.0f * s;
            const float bx1  = ctrlX1 - 56.0f * s;
            DrawProgressBar(rt, bx0, ctrlCY - kBarH * 0.5f, bx1, kBarH, kBarR, frac, sel, ease);

            // Numeric value label
            if (dwrite) {
                wchar_t valBuf[32];
                std::swprintf(valBuf, 32, L"%.2f%ls", *row.fTarget, row.unit ? row.unit : L"");
                Microsoft::WRL::ComPtr<IDWriteTextFormat> vf;
                dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                    DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                    kFVal, L"en-us", vf.GetAddressOf());
                if (vf) {
                    vf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                    vf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> vb;
                    rt->CreateSolidColorBrush(
                        sel ? Tok::GoldHi(0.96f*ease) : Tok::ChromeMute(0.66f*ease),
                        vb.GetAddressOf());
                    if (vb) rt->DrawText(valBuf,
                        static_cast<UINT32>(std::wcslen(valBuf)), vf.Get(),
                        D2D1::RectF(bx1 + 4.0f*s, rowY, ctrlX1, rowY + kRowH), vb.Get());
                }
            }
        }

        rowY += kRowH;
    }

    // ---- Footer hint bar
    {
        const float fy = panelY + kPanelH - kFooterH;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldDeep(0.18f * ease), b.GetAddressOf());
        if (b) rt->DrawLine(
            D2D1::Point2F(panelX + kPadX, fy),
            D2D1::Point2F(panelX + kPanelW - kPadX, fy),
            b.Get(), 0.6f);

        if (dwrite) {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> hf;
            dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                kFHint, L"en-us", hf.GetAddressOf());
            if (hf) {
                hf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                hf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hb;
                rt->CreateSolidColorBrush(Tok::ChromeMute(0.52f * ease), hb.GetAddressOf());
                if (hb) rt->DrawText(
                    L"\u25CF Toggle  \u00B7  \u2194 Adjust  \u00B7  \u25B2 Reset  \u00B7  \u25C6 Close",
                    static_cast<UINT32>(std::wcslen(
                        L"\u25CF Toggle  \u00B7  \u2194 Adjust  \u00B7  \u25B2 Reset  \u00B7  \u25C6 Close")),
                    hf.Get(),
                    D2D1::RectF(panelX + kPadX, fy, panelX + kPanelW - kPadX, panelY + kPanelH),
                    hb.Get());
            }
        }
    }
}

} // namespace enjoystick::overlay
