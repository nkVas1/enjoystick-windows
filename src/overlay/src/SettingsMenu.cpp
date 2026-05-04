#include <enjoystick/overlay/SettingsMenu.hpp>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

#include <cmath>
#include <algorithm>
#include <cwchar>

namespace enjoystick::overlay {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SettingsMenu::SettingsMenu(OnChangedCallback onChange)
    : m_onChange(std::move(onChange))
{
    BuildRows();
}

void SettingsMenu::BuildRows() {
    // Rows must stay in sync with the Values struct fields.
    m_rows = {
        { L"Cursor Speed",   RowType::FloatSlider, 200.f, 5000.f,  50.f,  &m_values.cursorSpeed,    nullptr, L" px/s" },
        { L"Curve",          RowType::FloatSlider, 0.5f,  3.0f,    0.1f,  &m_values.curveExponent,  nullptr, L""      },
        { L"Acceleration",   RowType::FloatSlider, 0.f,   500.f,   10.f,  &m_values.accelerationMs, nullptr, L" ms"   },
        { L"Scroll Speed",   RowType::FloatSlider, 1.f,   50.f,    1.f,   &m_values.scrollSpeed,    nullptr, L" t/s"  },
        { L"Deadzone Inner", RowType::FloatSlider, 0.0f,  0.45f,   0.01f, &m_values.dzInner,        nullptr, L""      },
        { L"Deadzone Outer", RowType::FloatSlider, 0.5f,  1.0f,    0.01f, &m_values.dzOuter,        nullptr, L""      },
        { L"Triggers",       RowType::BoolToggle,  0,     0,       0,     nullptr, &m_values.triggersAsClicks, L"" },
        { L"Cursor Stick",   RowType::BoolToggle,  0,     0,       0,     nullptr, &m_values.useRightStick,    L"" },
    };
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void SettingsMenu::Open(const Values& current) {
    if (m_state == State::Visible || m_state == State::Opening) return;
    m_values      = current;
    // Re-point row targets into the new m_values copy
    BuildRows();
    m_selectedRow = 0;
    m_state       = State::Opening;
}

void SettingsMenu::Close() {
    if (m_state == State::Hidden || m_state == State::Closing) return;
    m_state = State::Closing;
}

bool SettingsMenu::IsOpen() const noexcept {
    return m_state == State::Visible || m_state == State::Opening;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void SettingsMenu::Update(const ControllerState& state, float deltaSeconds) {
    UpdateAnimation(deltaSeconds);
    if (m_state == State::Hidden) return;

    const bool south  = HasButton(state.buttons, Button::South);
    const bool east   = HasButton(state.buttons, Button::East);
    const bool dUp    = HasButton(state.buttons, Button::DpadUp);
    const bool dDown  = HasButton(state.buttons, Button::DpadDown);
    const bool dLeft  = HasButton(state.buttons, Button::DpadLeft);
    const bool dRight = HasButton(state.buttons, Button::DpadRight);

    m_stickX = state.leftStick.x;

    // Navigation: D-pad Up/Down
    if (dUp && !m_prevDUp) {
        m_selectedRow = std::max(0, m_selectedRow - 1);
        m_repeatTimer = 0.0f;
    }
    if (dDown && !m_prevDDown) {
        m_selectedRow = std::min(static_cast<int32_t>(m_rows.size()) - 1,
                                 m_selectedRow + 1);
        m_repeatTimer = 0.0f;
    }

    // Value adjustment: D-pad Left/Right (with hold-repeat)
    const bool holdingLR = dLeft || dRight;
    if (holdingLR) {
        m_repeatTimer += deltaSeconds;
    } else {
        m_repeatTimer = 0.0f;
    }

    const float repeatInterval = 1.0f / kRepeatHz;
    const bool fire = (dLeft && !m_prevDLeft) ||
                      (dRight && !m_prevDRight) ||
                      (holdingLR && m_repeatTimer > 0.25f &&
                       std::fmod(m_repeatTimer, repeatInterval) <
                       deltaSeconds);
    if (fire) {
        const float dir = dRight ? 1.0f : -1.0f;
        AdjustSelected(dir, false);
    }

    // Fine control: left stick X (continuous while deflected)
    const float stickThreshold = 0.3f;
    if (std::abs(m_stickX) > stickThreshold) {
        // Apply a small nudge proportional to stick + delta time
        const float nudge = m_stickX * deltaSeconds * 3.0f;
        AdjustSelected(nudge, true);
    }

    // Toggle boolean with South
    if (south && !m_prevSouth) {
        const auto& row = m_rows[static_cast<size_t>(m_selectedRow)];
        if (row.type == RowType::BoolToggle && row.bTarget) {
            *row.bTarget = !*row.bTarget;
            CommitChange();
        }
    }

    // Close with East
    if (east && !m_prevEast) Close();

    m_prevSouth  = south;
    m_prevEast   = east;
    m_prevDUp    = dUp;
    m_prevDDown  = dDown;
    m_prevDLeft  = dLeft;
    m_prevDRight = dRight;
}

void SettingsMenu::UpdateAnimation(float deltaSeconds) {
    const float step = (deltaSeconds * 1000.0f) / kAnimMs;
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

void SettingsMenu::AdjustSelected(float direction, bool fine) {
    if (m_selectedRow < 0 ||
        m_selectedRow >= static_cast<int32_t>(m_rows.size())) return;
    auto& row = m_rows[static_cast<size_t>(m_selectedRow)];
    if (row.type != RowType::FloatSlider || !row.fTarget) return;

    const float delta = fine
        ? direction * row.step * 0.5f   // half-step for stick
        : direction * row.step;         // full step for D-pad

    *row.fTarget = std::clamp(*row.fTarget + delta, row.min, row.max);

    // Keep dzOuter > dzInner
    if (row.fTarget == &m_values.dzInner)
        m_values.dzOuter = std::max(m_values.dzOuter, m_values.dzInner + 0.05f);
    if (row.fTarget == &m_values.dzOuter)
        m_values.dzInner = std::min(m_values.dzInner, m_values.dzOuter - 0.05f);

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
    if (!renderTargetPtr) return;

    auto* rt     = static_cast<ID2D1RenderTarget*>(renderTargetPtr);
    auto* dwrite = static_cast<IDWriteFactory*>(dwriteFactoryPtr);

    const float scale = m_animProgress;

    // Panel geometry (slides in from the right)
    const float panelW = std::min(480.0f * dpiScale, screenW * 0.7f);
    const float panelH = std::min(520.0f * dpiScale, screenH * 0.85f);
    const float panelX = screenW - panelW * scale;   // slide-in
    const float panelY = (screenH - panelH) * 0.5f;

    // --- Panel background -----------------------------------------------
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bg;
        rt->CreateSolidColorBrush(
            D2D1::ColorF(0.09f, 0.09f, 0.11f, 0.90f * scale),
            bg.GetAddressOf());
        if (bg)
            rt->FillRoundedRectangle(
                D2D1::RoundedRect(
                    D2D1::RectF(panelX, panelY,
                                panelX + panelW, panelY + panelH),
                    12.0f * dpiScale, 12.0f * dpiScale),
                bg.Get());
    }

    if (!dwrite) return;

    // --- Title -----------------------------------------------------------
    {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFmt;
        dwrite->CreateTextFormat(
            L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            18.0f * dpiScale, L"en-us",
            titleFmt.GetAddressOf());
        if (titleFmt) {
            titleFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            titleFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> titleBrush;
            rt->CreateSolidColorBrush(
                D2D1::ColorF(0.96f, 0.96f, 1.0f, scale), titleBrush.GetAddressOf());
            if (titleBrush) {
                rt->DrawText(
                    L"\u2699  EnjoyStick Settings",
                    static_cast<UINT32>(std::wcslen(L"\u2699  EnjoyStick Settings")),
                    titleFmt.Get(),
                    D2D1::RectF(panelX + 8.0f * dpiScale,
                                panelY + 16.0f * dpiScale,
                                panelX + panelW - 8.0f * dpiScale,
                                panelY + 44.0f * dpiScale),
                    titleBrush.Get());
            }
        }
    }

    // --- Row hint ---------------------------------------------------------
    {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> hintFmt;
        dwrite->CreateTextFormat(
            L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            10.0f * dpiScale, L"en-us",
            hintFmt.GetAddressOf());
        if (hintFmt) {
            hintFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hintBrush;
            rt->CreateSolidColorBrush(
                D2D1::ColorF(0.6f, 0.6f, 0.7f, 0.8f * scale), hintBrush.GetAddressOf());
            if (hintBrush) {
                const wchar_t* hint =
                    L"\u2191\u2193 select   \u2190\u2192 adjust   A toggle   B close";
                rt->DrawText(
                    hint, static_cast<UINT32>(std::wcslen(hint)),
                    hintFmt.Get(),
                    D2D1::RectF(panelX, panelY + panelH - 24.0f * dpiScale,
                                panelX + panelW, panelY + panelH - 4.0f * dpiScale),
                    hintBrush.Get());
            }
        }
    }

    // --- Rows ------------------------------------------------------------
    const float rowH   = 48.0f * dpiScale;
    const float startY = panelY + 56.0f * dpiScale;
    const float padX   = 20.0f * dpiScale;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> labelFmt;
    dwrite->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        13.0f * dpiScale, L"en-us",
        labelFmt.GetAddressOf());
    if (labelFmt) labelFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

    Microsoft::WRL::ComPtr<IDWriteTextFormat> valueFmt;
    dwrite->CreateTextFormat(
        L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        13.0f * dpiScale, L"en-us",
        valueFmt.GetAddressOf());
    if (valueFmt) {
        valueFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
        valueFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    for (int32_t i = 0; i < static_cast<int32_t>(m_rows.size()); ++i) {
        const auto& row     = m_rows[static_cast<size_t>(i)];
        const bool  sel     = (i == m_selectedRow);
        const float ry      = startY + static_cast<float>(i) * rowH;
        const D2D1_RECT_F rowRect = D2D1::RectF(
            panelX + padX, ry,
            panelX + panelW - padX, ry + rowH - 4.0f * dpiScale);

        // Row highlight for selected
        if (sel) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selBrush;
            rt->CreateSolidColorBrush(
                D2D1::ColorF(0.31f, 0.27f, 0.90f, 0.25f * scale),
                selBrush.GetAddressOf());
            if (selBrush)
                rt->FillRoundedRectangle(
                    D2D1::RoundedRect(rowRect, 6.0f * dpiScale, 6.0f * dpiScale),
                    selBrush.Get());
        }

        // Slider track (float rows only)
        if (row.type == RowType::FloatSlider && row.fTarget) {
            const float trackX  = panelX + panelW * 0.42f;
            const float trackW  = panelW * 0.32f;
            const float trackMid = ry + rowH * 0.5f - 2.0f * dpiScale;

            // Background track
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> trackBg;
            rt->CreateSolidColorBrush(
                D2D1::ColorF(0.25f, 0.25f, 0.30f, 0.8f * scale),
                trackBg.GetAddressOf());
            if (trackBg)
                rt->FillRoundedRectangle(
                    D2D1::RoundedRect(
                        D2D1::RectF(trackX, trackMid,
                                    trackX + trackW, trackMid + 4.0f * dpiScale),
                        2.0f * dpiScale, 2.0f * dpiScale),
                    trackBg.Get());

            // Filled portion
            const float fraction = (row.max > row.min)
                ? (*row.fTarget - row.min) / (row.max - row.min)
                : 0.0f;
            const float fillW = trackW * std::clamp(fraction, 0.0f, 1.0f);
            if (fillW > 0.0f) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> trackFill;
                rt->CreateSolidColorBrush(
                    D2D1::ColorF(sel ? 0.55f : 0.40f,
                                 sel ? 0.50f : 0.38f,
                                 sel ? 0.98f : 0.85f,
                                 scale),
                    trackFill.GetAddressOf());
                if (trackFill)
                    rt->FillRoundedRectangle(
                        D2D1::RoundedRect(
                            D2D1::RectF(trackX, trackMid,
                                        trackX + fillW, trackMid + 4.0f * dpiScale),
                            2.0f * dpiScale, 2.0f * dpiScale),
                        trackFill.Get());
            }
        }

        // Label (left side)
        if (labelFmt) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> lBrush;
            rt->CreateSolidColorBrush(
                D2D1::ColorF(sel ? 1.0f : 0.80f,
                             sel ? 1.0f : 0.80f,
                             sel ? 1.0f : 0.84f,
                             scale),
                lBrush.GetAddressOf());
            if (lBrush)
                rt->DrawText(
                    row.label,
                    static_cast<UINT32>(std::wcslen(row.label)),
                    labelFmt.Get(),
                    D2D1::RectF(panelX + padX, ry,
                                panelX + panelW * 0.42f, ry + rowH - 4.0f * dpiScale),
                    lBrush.Get());
        }

        // Value text (right side)
        if (valueFmt) {
            wchar_t valStr[64];
            if (row.type == RowType::FloatSlider && row.fTarget) {
                // Use integer display if step >= 1, otherwise 2 decimals
                if (row.step >= 1.0f)
                    swprintf_s(valStr, 64, L"%.0f%ls", *row.fTarget, row.unit);
                else
                    swprintf_s(valStr, 64, L"%.2f%ls", *row.fTarget, row.unit);
            } else if (row.type == RowType::BoolToggle && row.bTarget) {
                const bool val = *row.bTarget;
                if (row.bTarget == &m_values.triggersAsClicks)
                    wcscpy_s(valStr, 64, val ? L"Clicks" : L"Scroll");
                else if (row.bTarget == &m_values.useRightStick)
                    wcscpy_s(valStr, 64, val ? L"Right" : L"Left");
                else
                    wcscpy_s(valStr, 64, val ? L"On" : L"Off");
            } else {
                valStr[0] = L'\0';
            }

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> vBrush;
            rt->CreateSolidColorBrush(
                D2D1::ColorF(sel ? 0.75f : 0.55f,
                             sel ? 0.72f : 0.52f,
                             sel ? 1.00f : 0.90f,
                             scale),
                vBrush.GetAddressOf());
            if (vBrush && valStr[0] != L'\0')
                rt->DrawText(
                    valStr,
                    static_cast<UINT32>(std::wcslen(valStr)),
                    valueFmt.Get(),
                    D2D1::RectF(panelX + panelW * 0.74f, ry,
                                panelX + panelW - padX, ry + rowH - 4.0f * dpiScale),
                    vBrush.Get());
        }
    }
}

} // namespace enjoystick::overlay
