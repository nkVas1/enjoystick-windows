#include <enjoystick/overlay/SettingsMenu.hpp>

// WIN32_LEAN_AND_MEAN and NOMINMAX are injected by CMake.
#include <Windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <format>

namespace enjoystick::overlay {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SettingsMenu::SettingsMenu(OnChangedCallback onChange)
    : m_onChange(std::move(onChange))
{
    BuildRows();
}

// ---------------------------------------------------------------------------
// BuildRows
// ---------------------------------------------------------------------------

void SettingsMenu::BuildRows() {
    m_rows.clear();
    m_rows.reserve(8);

    //  { label,              type,               min,   max,    step, fTarget,                       bTarget,                       unit }
    m_rows.push_back({ L"Cursor speed",    RowType::FloatSlider,  100.f, 4000.f, 50.f,  &m_values.cursorSpeed,    nullptr,                       L" px/s" });
    m_rows.push_back({ L"Curve exponent",  RowType::FloatSlider,  0.8f,  3.0f,   0.1f,  &m_values.curveExponent,  nullptr,                       L""      });
    m_rows.push_back({ L"Acceleration",    RowType::FloatSlider,  20.f,  500.f,  10.f,  &m_values.accelerationMs, nullptr,                       L" ms"   });
    m_rows.push_back({ L"Scroll speed",    RowType::FloatSlider,  1.f,   40.f,   0.5f,  &m_values.scrollSpeed,    nullptr,                       L""      });
    m_rows.push_back({ L"Deadzone inner",  RowType::FloatSlider,  0.02f, 0.40f,  0.01f, &m_values.dzInner,        nullptr,                       L""      });
    m_rows.push_back({ L"Deadzone outer",  RowType::FloatSlider,  0.50f, 1.00f,  0.01f, &m_values.dzOuter,        nullptr,                       L""      });
    m_rows.push_back({ L"Triggers as clicks", RowType::BoolToggle, 0, 0, 0,     nullptr,                       &m_values.triggersAsClicks,    L""      });
    m_rows.push_back({ L"Right stick moves cursor", RowType::BoolToggle, 0, 0, 0, nullptr,                     &m_values.useRightStick,       L""      });
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void SettingsMenu::Open(const Values& current) {
    m_values       = current;
    // Rebuild rows so pointer arithmetic re-binds to the new m_values copy
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

bool SettingsMenu::IsOpen() const noexcept {
    return m_state != State::Hidden;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void SettingsMenu::Update(const ControllerState& state, float deltaSeconds) {
    UpdateAnimation(deltaSeconds);
    if (m_state != State::Visible && m_state != State::Opening) return;

    // -----------------------------------------------------------------------
    // Button state (use HasButton helper - consistent with RadialMenu.cpp)
    // -----------------------------------------------------------------------
    const bool south  = HasButton(state.buttons, Button::South);
    const bool east   = HasButton(state.buttons, Button::East);
    const bool dUp    = HasButton(state.buttons, Button::DPadUp);
    const bool dDown  = HasButton(state.buttons, Button::DPadDown);
    const bool dLeft  = HasButton(state.buttons, Button::DPadLeft);
    const bool dRight = HasButton(state.buttons, Button::DPadRight);

    const bool southDown  = south  && !m_prevSouth;
    const bool eastDown   = east   && !m_prevEast;
    const bool dUpDown    = dUp    && !m_prevDUp;
    const bool dDownDown  = dDown  && !m_prevDDown;

    // -----------------------------------------------------------------------
    // East (B): close
    // -----------------------------------------------------------------------
    if (eastDown) {
        Close();
        goto update_prev;
    }

    // -----------------------------------------------------------------------
    // D-pad Up / Down: navigate rows
    // -----------------------------------------------------------------------
    if (dUpDown) {
        const int32_t n = static_cast<int32_t>(m_rows.size());
        m_selectedRow = (m_selectedRow - 1 + n) % n;
        m_repeatTimer = 0.0f;
    }
    if (dDownDown) {
        const int32_t n = static_cast<int32_t>(m_rows.size());
        m_selectedRow = (m_selectedRow + 1) % n;
        m_repeatTimer = 0.0f;
    }

    // -----------------------------------------------------------------------
    // South (A): toggle boolean
    // -----------------------------------------------------------------------
    if (southDown) {
        const auto& row = m_rows[m_selectedRow];
        if (row.type == RowType::BoolToggle && row.bTarget) {
            *row.bTarget = !(*row.bTarget);
            CommitChange();
        }
    }

    // -----------------------------------------------------------------------
    // D-pad Left / Right: adjust slider (with hold-repeat)
    // -----------------------------------------------------------------------
    {
        const bool hLeft  = dLeft;
        const bool hRight = dRight;
        const bool edgeL  = hLeft  && !m_prevDLeft;
        const bool edgeR  = hRight && !m_prevDRight;

        if (edgeL)  AdjustSelected(-1.0f, false);
        if (edgeR)  AdjustSelected( 1.0f, false);

        if (hLeft || hRight) {
            m_repeatTimer += deltaSeconds;
            const float interval = 1.0f / kRepeatHz;
            if (m_repeatTimer >= interval) {
                m_repeatTimer -= interval;
                if (hLeft)  AdjustSelected(-1.0f, true);
                if (hRight) AdjustSelected( 1.0f, true);
            }
        } else {
            m_repeatTimer = 0.0f;
        }
    }

    // -----------------------------------------------------------------------
    // Left stick X: fine continuous control
    // -----------------------------------------------------------------------
    {
        const float lx = state.leftStick.x;  // -1..1
        if (std::abs(lx) > 0.25f) {
            const float fine = lx * deltaSeconds * 2.0f;  // arbitrary scaling
            const auto& row = m_rows[m_selectedRow];
            if (row.type == RowType::FloatSlider && row.fTarget) {
                const float step = row.step * fine * 10.0f;
                *row.fTarget = std::clamp(*row.fTarget + step, row.min, row.max);
                CommitChange();
            }
        }
    }

update_prev:
    m_prevSouth  = south;
    m_prevEast   = east;
    m_prevDUp    = dUp;
    m_prevDDown  = dDown;
    m_prevDLeft  = dLeft;
    m_prevDRight = dRight;
    m_stickX     = state.leftStick.x;
}

// ---------------------------------------------------------------------------
// UpdateAnimation
// ---------------------------------------------------------------------------

void SettingsMenu::UpdateAnimation(float deltaSeconds) {
    const float speed = 1000.0f / kAnimMs;  // progress units per second

    switch (m_state) {
    case State::Opening:
        m_animProgress = std::min(1.0f, m_animProgress + speed * deltaSeconds);
        if (m_animProgress >= 1.0f) m_state = State::Visible;
        break;
    case State::Closing:
        m_animProgress = std::max(0.0f, m_animProgress - speed * deltaSeconds);
        if (m_animProgress <= 0.0f) m_state = State::Hidden;
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// AdjustSelected
// ---------------------------------------------------------------------------

void SettingsMenu::AdjustSelected(float direction, bool repeat) {
    const auto& row = m_rows[m_selectedRow];
    if (row.type != RowType::FloatSlider || !row.fTarget) return;

    const float step = repeat ? row.step * 2.0f : row.step;
    *row.fTarget = std::clamp(*row.fTarget + direction * step, row.min, row.max);
    CommitChange();
}

void SettingsMenu::CommitChange() {
    if (m_onChange) m_onChange(m_values);
}

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

    auto* rt      = static_cast<ID2D1RenderTarget*>(renderTargetPtr);
    auto* dwrite  = static_cast<IDWriteFactory*>(dwriteFactoryPtr);

    // -----------------------------------------------------------------------
    // Layout constants
    // -----------------------------------------------------------------------
    const float kPanelW    = 680.0f * dpiScale;
    const float kRowH      = 56.0f  * dpiScale;
    const float kHeaderH   = 72.0f  * dpiScale;
    const float kPadX      = 28.0f  * dpiScale;
    const float kPadY      = 20.0f  * dpiScale;
    const float kRadius    = 16.0f  * dpiScale;
    const float kBarH      = 3.0f   * dpiScale;
    const float kFontBody  = 15.0f  * dpiScale;
    const float kFontTitle = 19.0f  * dpiScale;
    const float kFontHint  = 12.0f  * dpiScale;

    const int32_t rowCount  = static_cast<int32_t>(m_rows.size());
    const float   kPanelH   = std::min(kHeaderH + kRowH * rowCount + kPadY,
                                       screenH * 0.80f);
    const float   panelY    = (screenH - kPanelH) * 0.5f;

    // Ease-out cubic: feel = 1 - (1-t)^3
    const float t    = m_animProgress;
    const float ease = 1.0f - std::pow(1.0f - t, 3.0f);

    // Slide in from the right
    const float targetX = screenW - kPanelW - 40.0f * dpiScale;
    const float panelX  = screenW - ease * (screenW - targetX);

    // -----------------------------------------------------------------------
    // Panel background
    // -----------------------------------------------------------------------
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bg;
        rt->CreateSolidColorBrush(
            D2D1::ColorF(0.07f, 0.07f, 0.10f, 0.94f * ease), bg.GetAddressOf());
        if (bg) {
            D2D1_ROUNDED_RECT rr{};
            rr.rect    = D2D1::RectF(panelX, panelY, panelX + kPanelW, panelY + kPanelH);
            rr.radiusX = rr.radiusY = kRadius;
            rt->FillRoundedRectangle(rr, bg.Get());
        }
    }

    // Subtle border
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bd;
        rt->CreateSolidColorBrush(
            D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f * ease), bd.GetAddressOf());
        if (bd) {
            D2D1_ROUNDED_RECT rr{};
            rr.rect    = D2D1::RectF(panelX + 0.5f, panelY + 0.5f,
                                     panelX + kPanelW - 0.5f, panelY + kPanelH - 0.5f);
            rr.radiusX = rr.radiusY = kRadius;

            Microsoft::WRL::ComPtr<ID2D1Factory> factory;
            rt->GetFactory(factory.GetAddressOf());
            if (factory) {
                Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> geo;
                factory->CreateRoundedRectangleGeometry(rr, geo.GetAddressOf());
                if (geo) rt->DrawGeometry(geo.Get(), bd.Get(), 1.0f);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Header
    // -----------------------------------------------------------------------
    {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtTitle;
        dwrite->CreateTextFormat(
            L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            kFontTitle, L"en-us", fmtTitle.GetAddressOf());
        if (fmtTitle) {
            fmtTitle->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> wb;
            rt->CreateSolidColorBrush(
                D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f * ease), wb.GetAddressOf());
            if (wb)
                rt->DrawText(
                    L"EnjoyStick  Settings",
                    20,
                    fmtTitle.Get(),
                    D2D1::RectF(panelX + kPadX, panelY,
                                panelX + kPanelW - kPadX, panelY + kHeaderH),
                    wb.Get());
        }

        // (B) close hint
        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtHint;
        dwrite->CreateTextFormat(
            L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            kFontHint, L"en-us", fmtHint.GetAddressOf());
        if (fmtHint) {
            fmtHint->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            fmtHint->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hintBrush;
            rt->CreateSolidColorBrush(
                D2D1::ColorF(0.6f, 0.6f, 0.65f, 0.85f * ease), hintBrush.GetAddressOf());
            if (hintBrush)
                rt->DrawText(
                    L"(B) Close",
                    9,
                    fmtHint.Get(),
                    D2D1::RectF(panelX + kPadX, panelY,
                                panelX + kPanelW - kPadX, panelY + kHeaderH),
                    hintBrush.Get());
        }
    }

    // -----------------------------------------------------------------------
    // Rows
    // -----------------------------------------------------------------------
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtRow;
    dwrite->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        kFontBody, L"en-us", fmtRow.GetAddressOf());
    if (!fmtRow) return;
    fmtRow->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtVal;
    dwrite->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        kFontBody, L"en-us", fmtVal.GetAddressOf());
    if (!fmtVal) return;
    fmtVal->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    fmtVal->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    const float rowsTop = panelY + kHeaderH;
    const float rowsBot = panelY + kPanelH - kPadY;

    for (int32_t i = 0; i < rowCount; ++i) {
        const float ry = rowsTop + i * kRowH;
        if (ry + kRowH > rowsBot) break;  // clipped by max-height

        const auto& row      = m_rows[i];
        const bool  selected = (i == m_selectedRow);

        // Selection highlight
        if (selected) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sel;
            rt->CreateSolidColorBrush(
                D2D1::ColorF(0.10f, 0.55f, 0.65f, 0.35f * ease), sel.GetAddressOf());
            if (sel) {
                D2D1_ROUNDED_RECT selRr{};
                selRr.rect    = D2D1::RectF(
                    panelX + 8.0f * dpiScale, ry + 4.0f * dpiScale,
                    panelX + kPanelW - 8.0f * dpiScale, ry + kRowH - 4.0f * dpiScale);
                selRr.radiusX = selRr.radiusY = 8.0f * dpiScale;
                rt->FillRoundedRectangle(selRr, sel.Get());
            }
        }

        // Label
        {
            const float alpha = selected ? 1.0f : 0.75f;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> lb;
            rt->CreateSolidColorBrush(
                D2D1::ColorF(0.92f, 0.92f, 0.95f, alpha * ease), lb.GetAddressOf());
            if (lb)
                rt->DrawText(
                    row.label,
                    static_cast<UINT32>(std::wcslen(row.label)),
                    fmtRow.Get(),
                    D2D1::RectF(panelX + kPadX, ry, panelX + kPanelW * 0.58f, ry + kRowH),
                    lb.Get());
        }

        // Value / toggle
        {
            wchar_t valBuf[64];
            if (row.type == RowType::BoolToggle && row.bTarget) {
                const wchar_t* onoff = *row.bTarget ? L"ON" : L"OFF";
                std::wmemcpy(valBuf, onoff, std::wcslen(onoff) + 1);
            } else if (row.type == RowType::FloatSlider && row.fTarget) {
                // Format based on step precision
                if (row.step < 0.05f)
                    std::swprintf(valBuf, 64, L"%.2f%ls", *row.fTarget, row.unit);
                else if (row.step < 1.0f)
                    std::swprintf(valBuf, 64, L"%.1f%ls", *row.fTarget, row.unit);
                else
                    std::swprintf(valBuf, 64, L"%.0f%ls", *row.fTarget, row.unit);
            } else {
                valBuf[0] = L'\0';
            }

            const float valAlpha = selected ? 1.0f : 0.65f;

            // Teal tint for selected value
            D2D1_COLOR_F valColor = selected
                ? D2D1::ColorF(0.35f, 0.85f, 0.90f, valAlpha * ease)
                : D2D1::ColorF(0.80f, 0.80f, 0.84f, valAlpha * ease);

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> vb;
            rt->CreateSolidColorBrush(valColor, vb.GetAddressOf());
            if (vb)
                rt->DrawText(
                    valBuf,
                    static_cast<UINT32>(std::wcslen(valBuf)),
                    fmtVal.Get(),
                    D2D1::RectF(panelX + kPanelW * 0.58f, ry,
                                panelX + kPanelW - kPadX, ry + kRowH),
                    vb.Get());
        }

        // Float slider progress bar (thin bar at the bottom of the row)
        if (row.type == RowType::FloatSlider && row.fTarget) {
            const float fraction = std::clamp(
                (*row.fTarget - row.min) / (row.max - row.min), 0.0f, 1.0f);

            const float barX0 = panelX + kPadX;
            const float barX1 = panelX + kPanelW - kPadX;
            const float barY  = ry + kRowH - kBarH - 4.0f * dpiScale;

            // Track
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> track;
            rt->CreateSolidColorBrush(
                D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f * ease), track.GetAddressOf());
            if (track)
                rt->FillRectangle(
                    D2D1::RectF(barX0, barY, barX1, barY + kBarH), track.Get());

            // Fill
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fill;
            const float fillAlpha = selected ? 0.85f : 0.40f;
            rt->CreateSolidColorBrush(
                D2D1::ColorF(0.20f, 0.70f, 0.80f, fillAlpha * ease), fill.GetAddressOf());
            if (fill)
                rt->FillRectangle(
                    D2D1::RectF(barX0, barY, barX0 + (barX1 - barX0) * fraction,
                                barY + kBarH),
                    fill.Get());
        }
    }
}

} // namespace enjoystick::overlay
