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

namespace enjoystick::overlay {

SettingsMenu::SettingsMenu(OnChangedCallback onChange)
    : m_onChange(std::move(onChange))
{ BuildRows(); }

void SettingsMenu::BuildRows() {
    m_rows.clear();
    m_rows.reserve(8);
    m_rows.push_back({ L"Cursor speed",           RowType::FloatSlider, 100.f, 4000.f, 50.f,  &m_values.cursorSpeed,      nullptr,                    L" px/s" });
    m_rows.push_back({ L"Curve exponent",          RowType::FloatSlider, 0.8f,  3.0f,   0.1f,  &m_values.curveExponent,    nullptr,                    L""      });
    m_rows.push_back({ L"Acceleration",            RowType::FloatSlider, 20.f,  500.f,  10.f,  &m_values.accelerationMs,   nullptr,                    L" ms"   });
    m_rows.push_back({ L"Scroll speed",            RowType::FloatSlider, 1.f,   40.f,   0.5f,  &m_values.scrollSpeed,      nullptr,                    L""      });
    m_rows.push_back({ L"Deadzone inner",          RowType::FloatSlider, 0.02f, 0.40f,  0.01f, &m_values.dzInner,          nullptr,                    L""      });
    m_rows.push_back({ L"Deadzone outer",          RowType::FloatSlider, 0.50f, 1.00f,  0.01f, &m_values.dzOuter,          nullptr,                    L""      });
    m_rows.push_back({ L"Triggers as clicks",      RowType::BoolToggle,  0,     0,      0,     nullptr,                    &m_values.triggersAsClicks, L""      });
    m_rows.push_back({ L"Right stick moves cursor",RowType::BoolToggle,  0,     0,      0,     nullptr,                    &m_values.useRightStick,    L""      });
}

void SettingsMenu::Open(const Values& current) {
    m_values      = current;
    BuildRows();
    m_selectedRow = 0;
    m_state       = State::Opening;
    m_animProgress= 0.0f;
    m_repeatTimer = 0.0f;
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

    if (east && !m_prevEast)   { Close(); goto done; }
    if (dUp  && !m_prevDUp)    { const int32_t n = static_cast<int32_t>(m_rows.size()); m_selectedRow = (m_selectedRow - 1 + n) % n; m_repeatTimer = 0.0f; }
    if (dDown&& !m_prevDDown)  { const int32_t n = static_cast<int32_t>(m_rows.size()); m_selectedRow = (m_selectedRow + 1) % n;     m_repeatTimer = 0.0f; }
    if (south&& !m_prevSouth)  {
        const auto& row = m_rows[m_selectedRow];
        if (row.type == RowType::BoolToggle && row.bTarget)
        { *row.bTarget = !(*row.bTarget); CommitChange(); }
    }
    {
        const bool hLeft = dLeft, hRight = dRight;
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
            const auto& row = m_rows[m_selectedRow];
            if (row.type == RowType::FloatSlider && row.fTarget) {
                *row.fTarget = std::clamp(*row.fTarget + lx * deltaSeconds * row.step * 10.0f, row.min, row.max);
                CommitChange();
            }
        }
    }
done:
    m_prevSouth  = south; m_prevEast  = east;
    m_prevDUp    = dUp;   m_prevDDown = dDown;
    m_prevDLeft  = dLeft; m_prevDRight= dRight;
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
    const auto& row = m_rows[m_selectedRow];
    if (row.type != RowType::FloatSlider || !row.fTarget) return;
    const float step = repeat ? row.step * 2.0f : row.step;
    *row.fTarget = std::clamp(*row.fTarget + direction * step, row.min, row.max);
    CommitChange();
}
void SettingsMenu::CommitChange() { if (m_onChange) m_onChange(m_values); }

// ---------------------------------------------------------------------------
// Draw  — Dark Regalia visual language
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
    const float s = dpiScale;

    const float kPanelW  = 680.0f * s;
    const float kRowH    =  56.0f * s;
    const float kHeaderH =  76.0f * s;
    const float kPadX    =  28.0f * s;
    const float kPadY    =  18.0f * s;
    const float kRadius  =  14.0f * s;
    const float kBarH    =   3.0f * s;
    const float kAccentW =   3.0f * s;
    const float kFBody   =  15.0f * s;
    const float kFTitle  =  19.0f * s;
    const float kFHint   =  12.0f * s;

    const int32_t rowCount = static_cast<int32_t>(m_rows.size());
    const float   kPanelH  = std::min(kHeaderH + kRowH * rowCount + kPadY, screenH * 0.82f);
    const float   panelY   = (screenH - kPanelH) * 0.5f;

    const float t    = m_animProgress;
    const float ease = 1.0f - std::pow(1.0f - t, 3.0f);
    const float targetX = screenW - kPanelW - 40.0f * s;
    const float panelX  = screenW - ease * (screenW - targetX);

    // ---- Panel background (DeepVoid obsidian) -------------------------------
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bg;
        rt->CreateSolidColorBrush(Tok::DeepVoid(0.93f * ease), bg.GetAddressOf());
        if (bg) {
            D2D1_ROUNDED_RECT rr{ D2D1::RectF(panelX, panelY, panelX+kPanelW, panelY+kPanelH), kRadius, kRadius };
            rt->FillRoundedRectangle(rr, bg.Get());
        }
    }
    // Gold hairline border
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bd;
        rt->CreateSolidColorBrush(Tok::GoldShadow(0.45f * ease), bd.GetAddressOf());
        if (bd) {
            D2D1_ROUNDED_RECT rr{ D2D1::RectF(panelX+0.5f, panelY+0.5f, panelX+kPanelW-0.5f, panelY+kPanelH-0.5f), kRadius, kRadius };
            Microsoft::WRL::ComPtr<ID2D1Factory> fac;
            rt->GetFactory(fac.GetAddressOf());
            if (fac) {
                Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> geo;
                fac->CreateRoundedRectangleGeometry(rr, geo.GetAddressOf());
                if (geo) rt->DrawGeometry(geo.Get(), bd.Get(), 1.0f);
            }
        }
    }

    // ---- Header title -------------------------------------------------------
    {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtT;
        dwrite->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            kFTitle, L"en-us", fmtT.GetAddressOf());
        if (fmtT) {
            fmtT->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tb;
            rt->CreateSolidColorBrush(Tok::SilverLt(0.95f * ease), tb.GetAddressOf());
            if (tb)
                rt->DrawText(L"EnjoyStick  Settings", 20, fmtT.Get(),
                    D2D1::RectF(panelX+kPadX, panelY, panelX+kPanelW-kPadX, panelY+kHeaderH), tb.Get());
        }
        // Gold underline rule below header
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ul;
        rt->CreateSolidColorBrush(Tok::GoldMid(0.40f * ease), ul.GetAddressOf());
        if (ul)
            rt->DrawLine(
                D2D1::Point2F(panelX + kPadX,            panelY + kHeaderH - 1.0f),
                D2D1::Point2F(panelX + kPanelW - kPadX,  panelY + kHeaderH - 1.0f),
                ul.Get(), 0.8f);
    }
    // (B) hint
    {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtH;
        dwrite->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            kFHint, L"en-us", fmtH.GetAddressOf());
        if (fmtH) {
            fmtH->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            fmtH->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hb;
            rt->CreateSolidColorBrush(Tok::SilverMute(0.75f * ease), hb.GetAddressOf());
            if (hb)
                rt->DrawText(L"(B) Close", 9, fmtH.Get(),
                    D2D1::RectF(panelX+kPadX, panelY, panelX+kPanelW-kPadX, panelY+kHeaderH), hb.Get());
        }
    }

    // ---- Rows ---------------------------------------------------------------
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtRow, fmtVal;
    dwrite->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        kFBody, L"en-us", fmtRow.GetAddressOf());
    dwrite->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        kFBody, L"en-us", fmtVal.GetAddressOf());
    if (!fmtRow || !fmtVal) return;
    fmtRow->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmtVal->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    fmtVal->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    const float rowsTop = panelY + kHeaderH;
    const float rowsBot = panelY + kPanelH - kPadY;

    for (int32_t i = 0; i < rowCount; ++i) {
        const float ry = rowsTop + static_cast<float>(i) * kRowH;
        if (ry + kRowH > rowsBot) break;

        const auto& row = m_rows[i];
        const bool  sel = (i == m_selectedRow);

        // Selected row: ObsidianBase fill + gold left accent bar
        if (sel) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> rb;
            rt->CreateSolidColorBrush(Tok::ObsidianBase(0.55f * ease), rb.GetAddressOf());
            if (rb) {
                D2D1_ROUNDED_RECT selRr{ D2D1::RectF(
                    panelX + 8.0f*s, ry + 4.0f*s,
                    panelX + kPanelW - 8.0f*s, ry + kRowH - 4.0f*s), 8.0f*s, 8.0f*s };
                rt->FillRoundedRectangle(selRr, rb.Get());
            }
            // Gold left accent bar
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ab;
            rt->CreateSolidColorBrush(Tok::GoldMid(0.90f * ease), ab.GetAddressOf());
            if (ab) {
                D2D1_ROUNDED_RECT acBar{ D2D1::RectF(
                    panelX + 8.0f*s, ry + 10.0f*s,
                    panelX + 8.0f*s + kAccentW, ry + kRowH - 10.0f*s), kAccentW*0.5f, kAccentW*0.5f };
                rt->FillRoundedRectangle(acBar, ab.Get());
            }
        }

        // Row label
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> lb;
            const auto lc = sel ? Tok::SilverLt(0.95f * ease) : Tok::SilverMid(0.70f * ease);
            rt->CreateSolidColorBrush(lc, lb.GetAddressOf());
            if (lb)
                rt->DrawText(row.label, static_cast<UINT32>(std::wcslen(row.label)), fmtRow.Get(),
                    D2D1::RectF(panelX + kPadX + kAccentW + 6.0f*s, ry,
                                panelX + kPanelW * 0.58f, ry + kRowH), lb.Get());
        }

        // Row value
        {
            wchar_t valBuf[64];
            if (row.type == RowType::BoolToggle && row.bTarget) {
                const wchar_t* s2 = *row.bTarget ? L"ON" : L"OFF";
                std::wmemcpy(valBuf, s2, std::wcslen(s2)+1);
            } else if (row.type == RowType::FloatSlider && row.fTarget) {
                if (row.step < 0.05f)      std::swprintf(valBuf, 64, L"%.2f%ls", *row.fTarget, row.unit);
                else if (row.step < 1.0f)  std::swprintf(valBuf, 64, L"%.1f%ls", *row.fTarget, row.unit);
                else                       std::swprintf(valBuf, 64, L"%.0f%ls", *row.fTarget, row.unit);
            } else { valBuf[0] = L'\0'; }

            D2D1_COLOR_F vc;
            if (row.type == RowType::BoolToggle && row.bTarget) {
                // Gold ON, muted OFF
                vc = *row.bTarget ? Tok::GoldHi(0.95f * ease) : Tok::SilverMute(0.65f * ease);
            } else {
                vc = sel ? Tok::GoldHi(0.95f * ease) : Tok::SilverMid(0.65f * ease);
            }
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> vb;
            rt->CreateSolidColorBrush(vc, vb.GetAddressOf());
            if (vb)
                rt->DrawText(valBuf, static_cast<UINT32>(std::wcslen(valBuf)), fmtVal.Get(),
                    D2D1::RectF(panelX + kPanelW*0.58f, ry,
                                panelX + kPanelW - kPadX, ry + kRowH), vb.Get());
        }

        // Float progress bar
        if (row.type == RowType::FloatSlider && row.fTarget) {
            const float frac = std::clamp((*row.fTarget - row.min) / (row.max - row.min), 0.0f, 1.0f);
            const float bx0  = panelX + kPadX;
            const float bx1  = panelX + kPanelW - kPadX;
            const float by   = ry + kRowH - kBarH - 4.0f*s;

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> track;
            rt->CreateSolidColorBrush(Tok::White(0.08f * ease), track.GetAddressOf());
            if (track) rt->FillRectangle(D2D1::RectF(bx0, by, bx1, by+kBarH), track.Get());

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fill;
            const auto fc = sel ? Tok::GoldMid(0.85f * ease) : Tok::GoldShadow(0.45f * ease);
            rt->CreateSolidColorBrush(fc, fill.GetAddressOf());
            if (fill) rt->FillRectangle(
                D2D1::RectF(bx0, by, bx0 + (bx1-bx0)*frac, by+kBarH), fill.Get());
        }

        // Bottom hairline divider (GoldShadow, very faint)
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> div;
            rt->CreateSolidColorBrush(Tok::GoldShadow(0.12f * ease), div.GetAddressOf());
            if (div)
                rt->DrawLine(
                    D2D1::Point2F(panelX + kPadX, ry + kRowH - 0.5f),
                    D2D1::Point2F(panelX + kPanelW - kPadX, ry + kRowH - 0.5f),
                    div.Get(), 0.6f);
        }
    }
}

} // namespace enjoystick::overlay
