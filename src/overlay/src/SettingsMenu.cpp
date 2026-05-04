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
    m_rows.reserve(12);
    m_rows.push_back({ L"Cursor speed",             RowType::FloatSlider, 1.0f,   20.0f,  0.5f,  &m_values.cursorSpeed,       nullptr,                    L" px/ms" });
    m_rows.push_back({ L"Curve exponent",            RowType::FloatSlider, 0.8f,   3.0f,   0.05f, &m_values.curveExponent,     nullptr,                    L""       });
    m_rows.push_back({ L"Acceleration ramp",         RowType::FloatSlider, 20.0f,  500.0f, 10.0f, &m_values.accelerationMs,    nullptr,                    L" ms"    });
    m_rows.push_back({ L"Adaptive speed",            RowType::BoolToggle,  0,      0,      0,     nullptr,                     &m_values.adaptiveSpeed,    L""       });
    m_rows.push_back({ L"Traversal time",            RowType::FloatSlider, 300.0f, 2000.f, 50.0f, &m_values.targetTraversalMs, nullptr,                    L" ms"    });
    m_rows.push_back({ L"DPI weight",                RowType::FloatSlider, 0.0f,   1.0f,   0.05f, &m_values.dpiWeight,         nullptr,                    L""       });
    m_rows.push_back({ L"Scroll speed",              RowType::FloatSlider, 1.0f,   40.0f,  0.5f,  &m_values.scrollSpeed,       nullptr,                    L""       });
    m_rows.push_back({ L"Deadzone inner",            RowType::FloatSlider, 0.02f,  0.40f,  0.01f, &m_values.dzInner,           nullptr,                    L""       });
    m_rows.push_back({ L"Deadzone outer",            RowType::FloatSlider, 0.50f,  1.00f,  0.01f, &m_values.dzOuter,           nullptr,                    L""       });
    m_rows.push_back({ L"Triggers as clicks",        RowType::BoolToggle,  0,      0,      0,     nullptr,                     &m_values.triggersAsClicks, L""       });
    m_rows.push_back({ L"Right stick moves cursor",  RowType::BoolToggle,  0,      0,      0,     nullptr,                     &m_values.useRightStick,    L""       });
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
    if (dUp  && !m_prevDUp)    { const int32_t n = static_cast<int32_t>(m_rows.size()); m_selectedRow = (m_selectedRow - 1 + n) % n; m_repeatTimer = 0.0f; }
    if (dDown && !m_prevDDown) { const int32_t n = static_cast<int32_t>(m_rows.size()); m_selectedRow = (m_selectedRow + 1) % n;     m_repeatTimer = 0.0f; }
    if (south && !m_prevSouth) {
        const auto& row = m_rows[m_selectedRow];
        if (row.type == RowType::BoolToggle && row.bTarget) {
            *row.bTarget = !(*row.bTarget);
            CommitChange();
        }
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
    m_prevSouth  = south; m_prevEast   = east;
    m_prevDUp    = dUp;   m_prevDDown  = dDown;
    m_prevDLeft  = dLeft; m_prevDRight = dRight;
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
// Draw — Futurist Glamour  (v2 — corrected specular, no oval artefacts)
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

    const float kPanelW  = 720.0f * s;
    const float kRowH    =  58.0f * s;
    const float kHeaderH =  72.0f * s;
    const float kPadX    =  28.0f * s;
    const float kPadY    =  16.0f * s;
    const float kRadius  =  16.0f * s;
    const float kBarH    =   4.0f * s;
    const float kBarR    =   2.0f * s;
    const float kAccentW =   3.0f * s;
    const float kFBody   =  14.5f * s;
    const float kFTitle  =  17.0f * s;
    const float kFHint   =  11.5f * s;

    const int32_t rowCount = static_cast<int32_t>(m_rows.size());
    const float   kPanelH  = std::min(kHeaderH + kRowH * rowCount + kPadY * 2.0f, screenH * 0.88f);
    const float   panelY   = (screenH - kPanelH) * 0.5f;
    const float   targetX  = screenW - kPanelW - 36.0f * s;
    const float   panelX   = screenW - ease * (screenW - targetX);

    // ---- Scrim ---------------------------------------------------------------
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> scrim;
        rt->CreateSolidColorBrush(Tok::Scrim(0.48f * ease), scrim.GetAddressOf());
        if (scrim) rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, screenW, screenH), scrim.Get());
    }

    // ---- Panel background ---------------------------------------------------
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bg;
        rt->CreateSolidColorBrush(Tok::SurfaceSunken(0.96f * ease), bg.GetAddressOf());
        if (bg) {
            D2D1_ROUNDED_RECT rr{ D2D1::RectF(panelX, panelY, panelX+kPanelW, panelY+kPanelH), kRadius, kRadius };
            rt->FillRoundedRectangle(rr, bg.Get());
        }
    }
    // Panel borders: outer glow + inner hairline
    {
        Microsoft::WRL::ComPtr<ID2D1Factory> fac;
        rt->GetFactory(fac.GetAddressOf());
        if (fac) {
            for (int pass = 0; pass < 2; ++pass) {
                const float inset = pass == 0 ? 0.0f : 0.5f;
                D2D1_ROUNDED_RECT rr{ D2D1::RectF(
                    panelX+inset, panelY+inset,
                    panelX+kPanelW-inset, panelY+kPanelH-inset), kRadius, kRadius };
                Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> geo;
                fac->CreateRoundedRectangleGeometry(rr, geo.GetAddressOf());
                if (geo) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bd;
                    const auto bc = (pass == 0)
                        ? Tok::GoldShadow(0.30f * ease)
                        : Tok::InkLine(0.80f * ease);
                    rt->CreateSolidColorBrush(bc, bd.GetAddressOf());
                    if (bd) rt->DrawGeometry(geo.Get(), bd.Get(), pass == 0 ? 1.5f : 0.7f);
                }
            }
            // Top-edge luminance line: a short horizontal line just inside
            // the top border — replaces the oval blob.
            // Drawn as a line from (panelX+kRadius) to (panelX+kRadius*3)
            // at panelY+1.5*s, 5% white — suggests a brushed-metal edge.
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sp;
                rt->CreateSolidColorBrush(Tok::White(0.05f * ease), sp.GetAddressOf());
                if (sp) rt->DrawLine(
                    D2D1::Point2F(panelX + kRadius,          panelY + 1.5f * s),
                    D2D1::Point2F(panelX + kRadius * 4.0f,   panelY + 1.5f * s),
                    sp.Get(), 1.0f);
            }
        }
    }

    // ---- Header -------------------------------------------------------------
    if (dwrite) {
        // Icon ⊞
        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtIcon;
        dwrite->CreateTextFormat(L"Segoe UI Emoji", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            20.0f * s, L"en-us", fmtIcon.GetAddressOf());
        if (fmtIcon) {
            fmtIcon->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ib;
            rt->CreateSolidColorBrush(Tok::GoldMid(0.70f * ease), ib.GetAddressOf());
            if (ib) rt->DrawText(L"\u229E", 1, fmtIcon.Get(),
                D2D1::RectF(panelX + kPadX, panelY, panelX + kPadX + 28.0f*s, panelY + kHeaderH), ib.Get());
        }
        // Title
        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtT;
        dwrite->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            kFTitle, L"en-us", fmtT.GetAddressOf());
        if (fmtT) {
            fmtT->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tb;
            rt->CreateSolidColorBrush(Tok::GoldHi(0.95f * ease), tb.GetAddressOf());
            if (tb) rt->DrawText(L"EnjoyStick  —  Settings", 23, fmtT.Get(),
                D2D1::RectF(panelX + kPadX + 34.0f*s, panelY,
                            panelX + kPanelW - kPadX,  panelY + kHeaderH), tb.Get());
        }
        // (B) Close hint
        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtH;
        dwrite->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            kFHint, L"en-us", fmtH.GetAddressOf());
        if (fmtH) {
            fmtH->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            fmtH->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hb;
            rt->CreateSolidColorBrush(Tok::ChromeMute(0.65f * ease), hb.GetAddressOf());
            if (hb) rt->DrawText(L"\u25C6  Close", 7, fmtH.Get(),
                D2D1::RectF(panelX + kPadX, panelY + 10.0f*s,
                            panelX + kPanelW - kPadX, panelY + kHeaderH * 0.5f), hb.Get());
        }
    }
    // Header underline
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ul;
        rt->CreateSolidColorBrush(Tok::GoldWarm(0.45f * ease), ul.GetAddressOf());
        if (ul) rt->DrawLine(
            D2D1::Point2F(panelX + kPadX,           panelY + kHeaderH - 1.0f),
            D2D1::Point2F(panelX + kPanelW - kPadX, panelY + kHeaderH - 1.0f),
            ul.Get(), 0.9f);
    }

    // ---- Rows ---------------------------------------------------------------
    if (!dwrite) return;
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

        // Row background (selected)
        if (sel) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> rb;
            rt->CreateSolidColorBrush(Tok::SurfaceRaised(0.65f * ease), rb.GetAddressOf());
            if (rb) {
                D2D1_ROUNDED_RECT selRr{ D2D1::RectF(
                    panelX + 8.0f*s, ry + 3.0f*s,
                    panelX + kPanelW - 8.0f*s, ry + kRowH - 3.0f*s), 9.0f*s, 9.0f*s };
                rt->FillRoundedRectangle(selRr, rb.Get());
            }
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ab;
            rt->CreateSolidColorBrush(Tok::GoldWarm(0.95f * ease), ab.GetAddressOf());
            if (ab) {
                D2D1_ROUNDED_RECT acBar{ D2D1::RectF(
                    panelX + 8.0f*s, ry + 10.0f*s,
                    panelX + 8.0f*s + kAccentW, ry + kRowH - 10.0f*s), kAccentW*0.5f, kAccentW*0.5f };
                rt->FillRoundedRectangle(acBar, ab.Get());
            }
        }

        // Label
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> lb;
            rt->CreateSolidColorBrush(
                sel ? Tok::ChromeHi(0.95f * ease) : Tok::ChromeMid(0.65f * ease),
                lb.GetAddressOf());
            if (lb) rt->DrawText(row.label, static_cast<UINT32>(std::wcslen(row.label)),
                fmtRow.Get(),
                D2D1::RectF(panelX + kPadX + kAccentW + 8.0f*s, ry,
                            panelX + kPanelW * 0.60f, ry + kRowH), lb.Get());
        }

        // Value
        {
            wchar_t valBuf[80];
            if (row.type == RowType::BoolToggle && row.bTarget) {
                std::wmemcpy(valBuf, *row.bTarget ? L"ON" : L"OFF", *row.bTarget ? 3 : 4);
            } else if (row.type == RowType::FloatSlider && row.fTarget) {
                if      (row.step < 0.05f) std::swprintf(valBuf, 80, L"%.2f%ls", *row.fTarget, row.unit);
                else if (row.step < 1.0f)  std::swprintf(valBuf, 80, L"%.1f%ls", *row.fTarget, row.unit);
                else                       std::swprintf(valBuf, 80, L"%.0f%ls", *row.fTarget, row.unit);
            } else { valBuf[0] = L'\0'; }

            if (row.type == RowType::BoolToggle && row.bTarget) {
                const float pillW = 44.0f * s;
                const float pillH = 22.0f * s;
                const float px0   = panelX + kPanelW - kPadX - pillW;
                const float py0   = ry + (kRowH - pillH) * 0.5f;
                const float pr    = pillH * 0.5f;
                if (*row.bTarget) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pb;
                    rt->CreateSolidColorBrush(Tok::GoldWarm(0.92f * ease), pb.GetAddressOf());
                    if (pb) { D2D1_ROUNDED_RECT pill{ D2D1::RectF(px0,py0,px0+pillW,py0+pillH), pr, pr }; rt->FillRoundedRectangle(pill, pb.Get()); }
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dot;
                    rt->CreateSolidColorBrush(Tok::ChromeHi(0.95f * ease), dot.GetAddressOf());
                    if (dot) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px0+pillW-pr, py0+pr), pr*0.62f, pr*0.62f), dot.Get());
                } else {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pb;
                    rt->CreateSolidColorBrush(Tok::InkLine(0.80f * ease), pb.GetAddressOf());
                    if (pb) { D2D1_ROUNDED_RECT pill{ D2D1::RectF(px0,py0,px0+pillW,py0+pillH), pr, pr }; rt->DrawRoundedRectangle(pill, pb.Get(), 1.2f); }
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dot;
                    rt->CreateSolidColorBrush(Tok::ChromeMute(0.65f * ease), dot.GetAddressOf());
                    if (dot) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px0+pr, py0+pr), pr*0.55f, pr*0.55f), dot.Get());
                }
            } else {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> vb;
                rt->CreateSolidColorBrush(
                    sel ? Tok::GoldHi(0.95f * ease) : Tok::ChromeMid(0.60f * ease),
                    vb.GetAddressOf());
                if (vb) rt->DrawText(valBuf, static_cast<UINT32>(std::wcslen(valBuf)), fmtVal.Get(),
                    D2D1::RectF(panelX + kPanelW*0.60f, ry,
                                panelX + kPanelW - kPadX, ry + kRowH), vb.Get());
            }
        }

        // Float progress bar
        if (row.type == RowType::FloatSlider && row.fTarget) {
            const float frac = std::clamp((*row.fTarget - row.min) / (row.max - row.min), 0.0f, 1.0f);
            const float bx0  = panelX + kPadX + kAccentW + 8.0f*s;
            const float bx1  = panelX + kPanelW - kPadX;
            const float by   = ry + kRowH - kBarH - 5.0f*s;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> track;
            rt->CreateSolidColorBrush(Tok::SurfaceRaised(0.90f * ease), track.GetAddressOf());
            if (track) { D2D1_ROUNDED_RECT tr{ D2D1::RectF(bx0, by, bx1, by+kBarH), kBarR, kBarR }; rt->FillRoundedRectangle(tr, track.Get()); }
            if (frac > 0.0f) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fill;
                rt->CreateSolidColorBrush(
                    sel ? Tok::GoldWarm(0.90f * ease) : Tok::GoldDeep(0.60f * ease),
                    fill.GetAddressOf());
                const float bxFill = bx0 + (bx1 - bx0) * frac;
                if (fill) { D2D1_ROUNDED_RECT fr{ D2D1::RectF(bx0, by, bxFill, by+kBarH), kBarR, kBarR }; rt->FillRoundedRectangle(fr, fill.Get()); }
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> knob;
                rt->CreateSolidColorBrush(
                    sel ? Tok::GoldAccent(0.95f * ease) : Tok::GoldMid(0.55f * ease),
                    knob.GetAddressOf());
                if (knob) rt->FillEllipse(
                    D2D1::Ellipse(D2D1::Point2F(bxFill, by + kBarH * 0.5f),
                                  kBarH * 0.85f, kBarH * 0.85f), knob.Get());
            }
        }

        // Row divider
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> div;
            rt->CreateSolidColorBrush(Tok::InkLine(0.55f * ease), div.GetAddressOf());
            if (div) rt->DrawLine(
                D2D1::Point2F(panelX + kPadX, ry + kRowH - 0.5f),
                D2D1::Point2F(panelX + kPanelW - kPadX, ry + kRowH - 0.5f),
                div.Get(), 0.6f);
        }
    }
}

} // namespace enjoystick::overlay
