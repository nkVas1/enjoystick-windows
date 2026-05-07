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
static constexpr float kPif = static_cast<float>(M_PI);
(void)kPif;

namespace enjoystick::overlay {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
SettingsMenu::SettingsMenu(OnChangedCallback onChange)
    : m_onChange(std::move(onChange))
{}

// ---------------------------------------------------------------------------
// BuildTabs  -- defines all 5 tabs and their rows
// ---------------------------------------------------------------------------
void SettingsMenu::BuildTabs() {
    m_tabs.clear();
    m_tabs.reserve(5);

    // --- Tab 0: Cursor
    {
        Tab t; t.title = L"Cursor";
        t.rows.push_back({ L"Cursor speed",       RowType::FloatSlider, 1.0f,   20.0f,  0.5f,  &m_values.cursorSpeed,       nullptr,                    L" px/ms" });
        t.rows.push_back({ L"Curve exponent",      RowType::FloatSlider, 0.8f,   3.0f,   0.05f, &m_values.curveExponent,     nullptr,                    L""       });
        t.rows.push_back({ L"Acceleration",        RowType::FloatSlider, 20.0f,  500.0f, 10.0f, &m_values.accelerationMs,    nullptr,                    L" ms"    });
        t.rows.push_back({ L"Right stick cursor",  RowType::BoolToggle,  0,      0,      0,     nullptr,                     &m_values.useRightStick,    L""       });
        m_tabs.push_back(std::move(t));
    }

    // --- Tab 1: Scrolling
    {
        Tab t; t.title = L"Scrolling";
        t.rows.push_back({ L"Scroll speed",        RowType::FloatSlider, 1.0f,   40.0f,  0.5f,  &m_values.scrollSpeed,       nullptr,                    L""       });
        t.rows.push_back({ L"Triggers as clicks",  RowType::BoolToggle,  0,      0,      0,     nullptr,                     &m_values.triggersAsClicks, L""       });
        m_tabs.push_back(std::move(t));
    }

    // --- Tab 2: Adaptive
    {
        Tab t; t.title = L"Adaptive";
        t.rows.push_back({ L"Adaptive speed",      RowType::BoolToggle,  0,      0,      0,     nullptr,                     &m_values.adaptiveSpeed,    L""       });
        t.rows.push_back({ L"Traversal time",      RowType::FloatSlider, 300.0f, 2000.f, 50.0f, &m_values.targetTraversalMs, nullptr,                    L" ms"    });
        t.rows.push_back({ L"DPI weight",          RowType::FloatSlider, 0.0f,   1.0f,   0.05f, &m_values.dpiWeight,         nullptr,                    L""       });
        m_tabs.push_back(std::move(t));
    }

    // --- Tab 3: Advanced
    {
        Tab t; t.title = L"Advanced";
        t.rows.push_back({ L"Deadzone inner",      RowType::FloatSlider, 0.02f,  0.40f,  0.01f, &m_values.dzInner,           nullptr,                    L""       });
        t.rows.push_back({ L"Deadzone outer",      RowType::FloatSlider, 0.50f,  1.00f,  0.01f, &m_values.dzOuter,           nullptr,                    L""       });
        m_tabs.push_back(std::move(t));
    }

    // --- Tab 4: Profile
    {
        Tab t; t.title = L"Profile";
        Row saveRow; saveRow.label = L"\U0001F4BE  Save profile"; saveRow.type = RowType::ActionButton; saveRow.actionId = ActionId::SaveProfile;
        Row loadRow; loadRow.label = L"\U0001F4C2  Load profile"; loadRow.type = RowType::ActionButton; loadRow.actionId = ActionId::LoadProfile;
        t.rows.push_back(saveRow);
        t.rows.push_back(loadRow);
        m_tabs.push_back(std::move(t));
    }
}

void SettingsMenu::RebuildToggleSprings() {
    size_t total = 0;
    for (const auto& tab : m_tabs) total += tab.rows.size();
    m_toggleSprings.resize(total);
    size_t idx = 0;
    for (const auto& tab : m_tabs) {
        for (const auto& row : tab.rows) {
            if (row.type == RowType::BoolToggle && row.bTarget) {
                m_toggleSprings[idx].stiffness = 420.0f;
                m_toggleSprings[idx].damping   = 26.0f;
                m_toggleSprings[idx].Snap(*row.bTarget ? 1.0f : 0.0f);
                m_toggleSprings[idx].SetTarget(*row.bTarget ? 1.0f : 0.0f);
            }
            ++idx;
        }
    }
}

static int32_t FlatRowIndex(const std::vector<SettingsMenu::Tab>& tabs, int32_t tabIdx, int32_t rowIdx) {
    int32_t flat = 0;
    for (int32_t t = 0; t < tabIdx; ++t)
        flat += static_cast<int32_t>(tabs[static_cast<size_t>(t)].rows.size());
    return flat + rowIdx;
}

bool SettingsMenu::IsInteractiveRow(int32_t tabIdx, int32_t rowIdx) const noexcept {
    if (tabIdx < 0 || tabIdx >= static_cast<int32_t>(m_tabs.size())) return false;
    const auto& rows = m_tabs[static_cast<size_t>(tabIdx)].rows;
    if (rowIdx < 0 || rowIdx >= static_cast<int32_t>(rows.size())) return false;
    return true;
}

int32_t SettingsMenu::NextInteractiveRow(int32_t tabIdx, int32_t from, int32_t dir) const noexcept {
    if (tabIdx < 0 || tabIdx >= static_cast<int32_t>(m_tabs.size())) return from;
    const int32_t n = static_cast<int32_t>(m_tabs[static_cast<size_t>(tabIdx)].rows.size());
    if (n == 0) return from;
    const int32_t next = from + dir;
    if (next < 0) return 0;
    if (next >= n) return n - 1;
    return next;
}

void SettingsMenu::SwitchTab(int32_t dir) {
    const int32_t nTabs = static_cast<int32_t>(m_tabs.size());
    if (nTabs == 0) return;
    m_activeTab = (m_activeTab + dir + nTabs) % nTabs;
    m_selectedRow   = 0;
    m_prevRow       = -1;
    m_trailAlpha    = 0.0f;
    m_selAnimT      = 1.0f;
    m_selCursorInit = false;
    if (m_onNavigate) m_onNavigate();
}

void SettingsMenu::OnRowChanged(int32_t newRow) {
    m_prevRow     = m_selectedRow;
    m_trailAlpha  = 1.0f;
    m_selAnimT    = 0.0f;
    m_selectedRow = newRow;
    m_selCursorSpring.velocity += 320.0f * (newRow > m_prevRow ? 1.0f : -1.0f);
    if (m_onNavigate) m_onNavigate();
}

void SettingsMenu::Open(const Values& current) {
    m_values       = current;
    BuildTabs();
    RebuildToggleSprings();
    m_activeTab    = 0;
    m_selectedRow  = 0;
    m_state        = State::Opening;
    m_animProgress = 0.0f;
    m_prevRow      = -1;
    m_trailAlpha   = 0.0f;
    m_selAnimT     = 1.0f;
    m_actionFlashAlpha = 0.0f;
    m_actionFlashRow   = -1;

    m_selCursorInit = false;
    m_selCursorSpring.stiffness = 600.0f;
    m_selCursorSpring.damping   = 30.0f;
    m_selCursorSpring.value     = 0.0f;
    m_selCursorSpring.velocity  = 0.0f;

    m_tabIndicatorInit = false;
    m_tabIndicatorSpring.stiffness = 420.0f;
    m_tabIndicatorSpring.damping   = 28.0f;
    m_tabIndicatorSpring.value     = 0.0f;
    m_tabIndicatorSpring.velocity  = 0.0f;

    m_stickNavActive   = false;
    m_stickNavCooldown = 0.0f;
    m_stickNavHoldTime = 0.0f;
    m_stickLxActive    = false;
    m_stickLxCooldown  = 0.0f;
    m_dpadVertHeld     = false;
    m_dpadVertTimer    = 0.0f;
    m_dpadHorzHeld     = false;
    m_dpadHorzTimer    = 0.0f;

    m_prevSouth = m_prevEast = m_prevNorth =
    m_prevDUp = m_prevDDown = m_prevDLeft = m_prevDRight =
    m_prevLB  = m_prevRB  = false;
}

void SettingsMenu::Close() {
    if (m_state == State::Hidden) return;
    m_state = State::Closing;
    m_stickNavActive = false;
    m_stickLxActive  = false;
    m_dpadVertHeld   = false;
    m_dpadHorzHeld   = false;
}

bool SettingsMenu::IsOpen() const noexcept { return m_state != State::Hidden; }

void SettingsMenu::ResetToDefaults() {
    m_values = Values{};
    BuildTabs();
    RebuildToggleSprings();
    m_activeTab   = 0;
    m_selectedRow = 0;
    m_prevRow     = -1;
    m_trailAlpha  = 0.0f;
    m_selAnimT    = 1.0f;
    CommitChange();
}

void SettingsMenu::ActivateSelected() {
    if (!IsInteractiveRow(m_activeTab, m_selectedRow)) return;
    const auto& row = m_tabs[static_cast<size_t>(m_activeTab)].rows[static_cast<size_t>(m_selectedRow)];

    if (row.type == RowType::BoolToggle && row.bTarget) {
        *row.bTarget = !(*row.bTarget);
        CommitChange();
        const int32_t flat = FlatRowIndex(m_tabs, m_activeTab, m_selectedRow);
        if (flat >= 0 && flat < static_cast<int32_t>(m_toggleSprings.size()))
            m_toggleSprings[static_cast<size_t>(flat)].SetTarget(*row.bTarget ? 1.0f : 0.0f);
    } else if (row.type == RowType::ActionButton) {
        m_actionFlashAlpha = 1.0f;
        m_actionFlashRow   = m_selectedRow;
        if (m_onAction) m_onAction(row.actionId);
    }
}

void SettingsMenu::Update(const ControllerState& state, float dt) {
    {
        size_t flat = 0;
        for (const auto& tab : m_tabs) {
            for (const auto& row : tab.rows) {
                if (flat < m_toggleSprings.size() && row.type == RowType::BoolToggle && row.bTarget) {
                    m_toggleSprings[flat].SetTarget(*row.bTarget ? 1.0f : 0.0f);
                    m_toggleSprings[flat].Step(dt);
                }
                ++flat;
            }
        }
    }
    m_selCursorSpring.Step(dt);
    m_tabIndicatorSpring.Step(dt);

    if (m_actionFlashAlpha > 0.0f) {
        m_actionFlashAlpha -= dt * 3.5f;
        if (m_actionFlashAlpha < 0.0f) {
            m_actionFlashAlpha = 0.0f;
            m_actionFlashRow   = -1;
        }
    }

    UpdateAnimation(dt);
    if (m_state != State::Visible && m_state != State::Opening) return;

    const bool south  = HasButton(state.buttons, Button::South);
    const bool east   = HasButton(state.buttons, Button::East);
    const bool north  = HasButton(state.buttons, Button::North);
    const bool lb     = HasButton(state.buttons, Button::LB);
    const bool rb     = HasButton(state.buttons, Button::RB);
    const bool dUp    = HasButton(state.buttons, Button::DPadUp);
    const bool dDown  = HasButton(state.buttons, Button::DPadDown);
    const bool dLeft  = HasButton(state.buttons, Button::DPadLeft);
    const bool dRight = HasButton(state.buttons, Button::DPadRight);

    if (east  && !m_prevEast)  { Close(); goto done; }
    if (north && !m_prevNorth) { ResetToDefaults(); goto done; }
    if (south && !m_prevSouth) { ActivateSelected(); }

    if (lb && !m_prevLB) { SwitchTab(-1); }
    if (rb && !m_prevRB) { SwitchTab(+1); }

    {
        const bool dVert = dUp || dDown;
        if (dVert) {
            if (!m_dpadVertHeld) {
                m_dpadVertHeld  = true;
                m_dpadVertTimer = kSnapFirst;
                const int32_t next = NextInteractiveRow(m_activeTab, m_selectedRow, dUp ? -1 : 1);
                if (next != m_selectedRow) OnRowChanged(next);
            } else {
                m_dpadVertTimer -= dt;
                if (m_dpadVertTimer <= 0.0f) {
                    m_dpadVertTimer = kSnapNext;
                    const int32_t next = NextInteractiveRow(m_activeTab, m_selectedRow, dUp ? -1 : 1);
                    if (next != m_selectedRow) OnRowChanged(next);
                }
            }
        } else {
            m_dpadVertHeld  = false;
            m_dpadVertTimer = 0.0f;
        }
    }

    {
        const bool hHorz = dLeft || dRight;
        if (hHorz) {
            if (!m_dpadHorzHeld) {
                m_dpadHorzHeld  = true;
                m_dpadHorzTimer = kSnapFirst;
                if (dLeft)  AdjustSelected(-1.0f, false);
                if (dRight) AdjustSelected( 1.0f, false);
            } else {
                m_dpadHorzTimer -= dt;
                if (m_dpadHorzTimer <= 0.0f) {
                    m_dpadHorzTimer = kSnapNext;
                    if (dLeft)  AdjustSelected(-1.0f, true);
                    if (dRight) AdjustSelected( 1.0f, true);
                }
            }
        } else {
            m_dpadHorzHeld  = false;
            m_dpadHorzTimer = 0.0f;
        }
    }

    {
        const float ly = state.leftStick.y;
        const float lx = state.leftStick.x;
        const bool  lyActive = std::abs(ly) > kNavDeadzone;
        const bool  lxActive = std::abs(lx) > 0.30f;

        if (lyActive && !m_stickLxActive) {
            const int dir = (ly > 0.0f) ? -1 : 1;
            if (!m_stickNavActive) {
                m_stickNavActive   = true;
                m_stickNavCooldown = kSnapFirst;
                m_stickNavHoldTime = 0.0f;
                const int32_t next = NextInteractiveRow(m_activeTab, m_selectedRow, dir);
                if (next != m_selectedRow) OnRowChanged(next);
            } else {
                m_stickNavHoldTime += dt;
                m_stickNavCooldown -= dt;
                if (m_stickNavCooldown <= 0.0f) {
                    const float accelT   = std::max(0.0f,
                        (m_stickNavHoldTime - kNavAccelStart) / kNavAccelRange);
                    const float blend    = std::min(1.0f, accelT * accelT);
                    const float interval = kSnapNext + (kSnapFast - kSnapNext) * blend;
                    m_stickNavCooldown   = interval;
                    const int32_t next = NextInteractiveRow(m_activeTab, m_selectedRow, dir);
                    if (next != m_selectedRow) OnRowChanged(next);
                }
            }
        } else if (!lyActive) {
            m_stickNavActive   = false;
            m_stickNavCooldown = 0.0f;
            m_stickNavHoldTime = 0.0f;
        }

        if (lxActive && !m_stickNavActive) {
            if (!m_stickLxActive) {
                m_stickLxActive   = true;
                m_stickLxCooldown = kSnapFirst;
                const auto& row = m_tabs[static_cast<size_t>(m_activeTab)].rows[static_cast<size_t>(m_selectedRow)];
                if (row.type == RowType::FloatSlider && row.fTarget) {
                    *row.fTarget = std::clamp(
                        *row.fTarget + (lx > 0 ? 1.0f : -1.0f) * row.step,
                        row.min, row.max);
                    CommitChange();
                    if (m_onAdjust) m_onAdjust();
                }
            } else {
                m_stickLxCooldown -= dt;
                if (m_stickLxCooldown <= 0.0f) {
                    m_stickLxCooldown = kSnapNext * 0.6f;
                    const auto& row = m_tabs[static_cast<size_t>(m_activeTab)].rows[static_cast<size_t>(m_selectedRow)];
                    if (row.type == RowType::FloatSlider && row.fTarget) {
                        *row.fTarget = std::clamp(
                            *row.fTarget + (lx > 0 ? 1.0f : -1.0f) * row.step,
                            row.min, row.max);
                        CommitChange();
                        if (m_onAdjust) m_onAdjust();
                    }
                }
            }
        } else if (!lxActive) {
            m_stickLxActive   = false;
            m_stickLxCooldown = 0.0f;
        }
    }

done:
    m_prevSouth = south; m_prevEast  = east;  m_prevNorth = north;
    m_prevDUp   = dUp;   m_prevDDown = dDown;
    m_prevDLeft = dLeft; m_prevDRight = dRight;
    m_prevLB    = lb;    m_prevRB    = rb;
}

void SettingsMenu::UpdateAnimation(float dt) {
    const float step = dt * 1000.0f / kAnimMs;
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

    if (m_trailAlpha > 0.0f) {
        m_trailAlpha -= dt * 1000.0f / kTrailDecayMs;
        if (m_trailAlpha < 0.0f) m_trailAlpha = 0.0f;
    }

    if (m_selAnimT < 1.0f) {
        m_selAnimT += dt * kSelAnimSpeed;
        if (m_selAnimT > 1.0f) m_selAnimT = 1.0f;
    }
}

void SettingsMenu::AdjustSelected(float direction, bool repeat) {
    if (!IsInteractiveRow(m_activeTab, m_selectedRow)) return;
    const auto& row = m_tabs[static_cast<size_t>(m_activeTab)].rows[static_cast<size_t>(m_selectedRow)];
    if (row.type != RowType::FloatSlider || !row.fTarget) return;
    const float step = repeat ? row.step * 1.5f : row.step;
    *row.fTarget = std::clamp(*row.fTarget + direction * step, row.min, row.max);
    CommitChange();
    if (m_onAdjust) m_onAdjust();
}

void SettingsMenu::CommitChange() { if (m_onChange) m_onChange(m_values); }

// ---------------------------------------------------------------------------
// Draw helpers
// ---------------------------------------------------------------------------
namespace {

static float SelPopScale(float t) noexcept {
    if (t >= 1.0f) return 1.0f;
    const float A = 0.22f, k = 14.0f, w = 22.0f;
    return 1.0f + A * std::exp(-k * t) * std::cos(w * t);
}

static void DrawHintChip(
    ID2D1RenderTarget* rt, IDWriteFactory* dwrite,
    float x, float cy, float s, float ease,
    const wchar_t* label,
    const D2D1_COLOR_F& chipCol,
    const D2D1_COLOR_F& textCol,
    float fontSize) noexcept
{
    if (!rt || !dwrite) return;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    dwrite->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, fontSize, L"en-us", fmt.GetAddressOf());
    if (!fmt) return;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> lay;
    dwrite->CreateTextLayout(label, static_cast<UINT32>(std::wcslen(label)),
        fmt.Get(), 300.0f * s, 28.0f * s, lay.GetAddressOf());
    if (!lay) return;
    DWRITE_TEXT_METRICS tm{};
    lay->GetMetrics(&tm);
    const float padH = 7.0f * s, padV = 4.0f * s;
    const float cw   = tm.width + padH * 2.0f;
    const float ch   = tm.height + padV * 2.0f;
    const float cy0  = cy - ch * 0.5f;
    { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
      rt->CreateSolidColorBrush(chipCol, b.GetAddressOf());
      if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(x, cy0, x+cw, cy0+ch), 4.0f*s, 4.0f*s};
               rt->FillRoundedRectangle(rr, b.Get()); } }
    { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
      rt->CreateSolidColorBrush(textCol, b.GetAddressOf());
      if (b) rt->DrawTextLayout(D2D1::Point2F(x + padH, cy0 + padV), lay.Get(), b.Get()); }
    (void)ease;
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
    if (m_state == State::Hidden) return;
    const float ease = m_animProgress;
    if (ease <= 0.001f) return;
    if (!renderTargetPtr) return;

    auto* rt     = static_cast<ID2D1RenderTarget*>(renderTargetPtr);
    auto* dwrite = static_cast<IDWriteFactory*>(dwriteFactoryPtr);

    // Build ellipsis trimming object once (lazy init)
    if (dwrite && !m_dwriteEllipsis) {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> proto;
        dwrite->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 13.0f * dpiScale,
            L"en-us", proto.GetAddressOf());
        if (proto)
            dwrite->CreateEllipsisTrimmingSign(proto.Get(), m_dwriteEllipsis.GetAddressOf());
    }

    Microsoft::WRL::ComPtr<ID2D1Factory> fac;
    rt->GetFactory(fac.GetAddressOf());

    const float s        = dpiScale;
    const float pw       = 580.0f * s;
    const float ph_row   = 48.0f  * s;
    const float padX     = 28.0f  * s;
    const float padY     = 18.0f  * s;
    const float gap      =  6.0f  * s;
    const float cr       = 14.0f  * s;
    const float accentH  =  3.0f  * s;
    const float tabBarH  = 38.0f  * s;
    const float hintBarH = 36.0f  * s;

    const int32_t nTabs  = static_cast<int32_t>(m_tabs.size());
    const int32_t nRows  = (m_activeTab >= 0 && m_activeTab < nTabs)
        ? static_cast<int32_t>(m_tabs[static_cast<size_t>(m_activeTab)].rows.size())
        : 0;

    const float listH  = static_cast<float>(nRows) * (ph_row + gap) - (nRows > 0 ? gap : 0.0f);
    const float totalH = padY + accentH + tabBarH + gap + listH + gap + hintBarH + padY;

    const float px = (screenW - pw) * 0.5f;
    const float py = (screenH - totalH) * 0.5f;
    const float listAreaTop = py + padY + accentH + tabBarH + gap;

    // ---- Scrim
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::Scrim(0.55f * ease), b.GetAddressOf());
        if (b) rt->FillRectangle(D2D1::RectF(0,0,screenW,screenH), b.Get());
    }
    // ---- Panel fill
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceBase(0.97f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(px,py,px+pw,py+totalH),cr,cr};
                 rt->FillRoundedRectangle(rr, b.Get()); }
    }
    // ---- Gold accent top bar
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldMid(0.88f * ease), b.GetAddressOf());
        if (b) {
            D2D1_ROUNDED_RECT rr{D2D1::RectF(px,py,px+pw,py+accentH+cr),cr,cr};
            rt->FillRoundedRectangle(rr, b.Get());
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b2;
            rt->CreateSolidColorBrush(Tok::SurfaceBase(0.97f * ease), b2.GetAddressOf());
            if (b2) rt->FillRectangle(D2D1::RectF(px, py+accentH, px+pw, py+accentH+cr), b2.Get());
        }
    }
    // ---- Panel chrome border
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldShadow(0.30f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(px,py,px+pw,py+totalH),cr,cr};
                 rt->DrawRoundedRectangle(rr, b.Get(), 1.8f); }
    }

    // ---------------------------------------------------------------------------
    // Tab bar
    // ---------------------------------------------------------------------------
    const float tabAreaTop = py + padY + accentH;
    const float tabW = pw / static_cast<float>(nTabs);
    const float tabH = tabBarH;

    if (!m_tabIndicatorInit) {
        m_tabIndicatorSpring.Snap(static_cast<float>(m_activeTab));
        m_tabIndicatorInit = true;
    }
    m_tabIndicatorSpring.SetTarget(static_cast<float>(m_activeTab));

    // Background pill
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceSunken(0.55f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(px+6.0f*s, tabAreaTop+3.0f*s,
                    px+pw-6.0f*s, tabAreaTop+tabH-3.0f*s), 8.0f*s, 8.0f*s };
                 rt->FillRoundedRectangle(rr, b.Get()); }
    }

    // Animated active-tab indicator
    {
        const float indX = px + 6.0f*s + m_tabIndicatorSpring.value * tabW;
        const float indW = tabW;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldDeep(0.72f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(indX+2.0f*s, tabAreaTop+5.0f*s,
                    indX+indW-2.0f*s, tabAreaTop+tabH-5.0f*s), 7.0f*s, 7.0f*s };
                 rt->FillRoundedRectangle(rr, b.Get()); }
    }

    // Tab labels
    if (dwrite) {
        for (int32_t ti = 0; ti < nTabs; ++ti) {
            const float tx = px + 6.0f*s + static_cast<float>(ti) * tabW;
            const bool  active = (ti == m_activeTab);
            Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
            dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                active ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                11.5f * s, L"en-us", fmt.GetAddressOf());
            if (!fmt) continue;
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            const D2D1_COLOR_F col = active
                ? Tok::GoldBright(0.97f * ease)
                : Tok::ChromeMute(0.60f * ease);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(col, b.GetAddressOf());
            const wchar_t* title = m_tabs[static_cast<size_t>(ti)].title;
            if (b && title) rt->DrawText(title, static_cast<UINT32>(std::wcslen(title)),
                fmt.Get(), D2D1::RectF(tx, tabAreaTop, tx+tabW, tabAreaTop+tabH), b.Get());
        }
    }

    // Separator under tab bar
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldDeep(0.22f * ease), b.GetAddressOf());
        if (b) rt->DrawLine(
            D2D1::Point2F(px + padX, listAreaTop - gap * 0.5f),
            D2D1::Point2F(px + pw - padX, listAreaTop - gap * 0.5f),
            b.Get(), 0.8f);
    }

    // ---------------------------------------------------------------------------
    // Selection cursor spring: compute target Y
    // ---------------------------------------------------------------------------
    {
        const float selPixY = (nRows > 0)
            ? listAreaTop + static_cast<float>(m_selectedRow) * (ph_row + gap) + ph_row * 0.5f
            : listAreaTop + ph_row * 0.5f;

        if (!m_selCursorInit) {
            m_selCursorSpring.Snap(selPixY);
            m_selCursorInit = true;
        }
        m_selCursorSpring.SetTarget(selPixY);
    }
    const float cursorY = m_selCursorSpring.value;

    // ---------------------------------------------------------------------------
    // Rows
    // ---------------------------------------------------------------------------
    if (m_activeTab >= 0 && m_activeTab < nTabs) {
        const auto& activeRows = m_tabs[static_cast<size_t>(m_activeTab)].rows;
        int32_t flatBase = 0;
        for (int32_t ti = 0; ti < m_activeTab; ++ti)
            flatBase += static_cast<int32_t>(m_tabs[static_cast<size_t>(ti)].rows.size());

        for (int32_t i = 0; i < static_cast<int32_t>(activeRows.size()); ++i) {
            const auto& row  = activeRows[static_cast<size_t>(i)];
            const bool  sel  = (i == m_selectedRow);
            const bool  prev = (i == m_prevRow);
            const float ry   = listAreaTop + static_cast<float>(i) * (ph_row + gap);
            const float rh   = ph_row;
            const int32_t flat = flatBase + i;

            const float wx  = px + pw * 0.5f;
            const float wx1 = px + pw - padX - 4.0f * s;

            // Trail highlight
            if (prev && m_trailAlpha > 0.0f) {
                const float ta = m_trailAlpha * m_trailAlpha * ease;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tb;
                rt->CreateSolidColorBrush(Tok::GoldMid(0.55f * ta), tb.GetAddressOf());
                if (tb) { D2D1_ROUNDED_RECT rr{D2D1::RectF(px+8.0f*s,ry+1.0f*s,px+pw-8.0f*s,ry+rh-1.0f*s),7.0f*s,7.0f*s};
                          rt->FillRoundedRectangle(rr, tb.Get()); }
            }

            // Animated selection highlight
            if (sel) {
                const float popSc  = SelPopScale(m_selAnimT);
                const float halfH  = ph_row * 0.5f * popSc;
                const float selTop = cursorY - halfH + 1.0f * s;
                const float selBot = cursorY + halfH - 1.0f * s;

                const D2D1_COLOR_F selFill = (row.type == RowType::ActionButton)
                    ? Tok::GoldDeep(0.55f * ease)
                    : Tok::SurfaceRaised(0.80f * ease);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(selFill, b.GetAddressOf());
                if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(px+8.0f*s,selTop,px+pw-8.0f*s,selBot),7.0f*s,7.0f*s};
                         rt->FillRoundedRectangle(rr, b.Get()); }

                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ab;
                rt->CreateSolidColorBrush(Tok::GoldHi(0.85f * ease), ab.GetAddressOf());
                if (ab) { D2D1_ROUNDED_RECT rr{D2D1::RectF(px+8.0f*s, cursorY-ph_row*0.5f+6.0f*s,
                              px+11.5f*s, cursorY+ph_row*0.5f-6.0f*s),1.5f*s,1.5f*s};
                          rt->FillRoundedRectangle(rr, ab.Get()); }
            }

            // ActionButton press flash
            if (row.type == RowType::ActionButton && i == m_actionFlashRow && m_actionFlashAlpha > 0.0f) {
                const float fa = m_actionFlashAlpha * ease;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fb;
                rt->CreateSolidColorBrush(Tok::GoldBright(0.72f * fa), fb.GetAddressOf());
                if (fb) { D2D1_ROUNDED_RECT rr{D2D1::RectF(px+8.0f*s,ry+1.0f*s,px+pw-8.0f*s,ry+rh-1.0f*s),7.0f*s,7.0f*s};
                          rt->FillRoundedRectangle(rr, fb.Get()); }
            }

            // Row label
            if (dwrite && row.label) {
                const bool isAction = (row.type == RowType::ActionButton);
                const float lblFontSz = isAction ? 14.5f * s : 13.5f * s;
                Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
                dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                    isAction ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_SEMI_BOLD,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                    lblFontSz, L"en-us", fmt.GetAddressOf());
                if (fmt) {
                    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    if (isAction) fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

                    Microsoft::WRL::ComPtr<IDWriteTextLayout> lay;
                    const float maxW = isAction ? (pw - padX * 2.0f) : (pw * 0.5f - padX - 8.0f * s);
                    dwrite->CreateTextLayout(row.label, static_cast<UINT32>(std::wcslen(row.label)),
                        fmt.Get(), maxW, ph_row, lay.GetAddressOf());
                    if (lay) {
                        lay->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                        if (!isAction && m_dwriteEllipsis) {
                            DWRITE_TRIMMING trimming{};
                            trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
                            lay->SetTrimming(&trimming, m_dwriteEllipsis.Get());
                        }
                        const D2D1_COLOR_F lblCol = isAction
                            ? (sel ? Tok::GoldBright(0.98f*ease) : Tok::GoldMid(0.80f*ease))
                            : (sel ? Tok::GoldHi(0.96f*ease)     : Tok::ChromeHi(0.82f*ease));
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                        rt->CreateSolidColorBrush(lblCol, b.GetAddressOf());
                        const float originX = isAction ? px + padX : px + padX + 6.0f * s;
                        if (b) rt->DrawTextLayout(
                            D2D1::Point2F(originX, ry + (ph_row - lblFontSz * 1.4f) * 0.5f),
                            lay.Get(), b.Get());
                    }
                }
            }

            // Value widget (right half)
            if (row.type == RowType::FloatSlider && row.fTarget) {
                const float t   = std::clamp((*row.fTarget - row.min) / (row.max - row.min + 0.0001f), 0.0f, 1.0f);
                const float bcy = ry + rh * 0.5f;
                const float bh2 = 5.0f * s;
                { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                  rt->CreateSolidColorBrush(Tok::SurfaceSunken(0.90f * ease), b.GetAddressOf());
                  if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(wx,bcy-bh2,wx1,bcy+bh2),bh2,bh2};
                           rt->FillRoundedRectangle(rr, b.Get()); } }
                { const float fillX = wx + (wx1-wx)*t;
                  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                  rt->CreateSolidColorBrush(
                      sel ? Tok::GoldMid(0.88f*ease) : Tok::GoldDeep(0.60f*ease), b.GetAddressOf());
                  if (b && fillX > wx) { D2D1_ROUNDED_RECT rr{D2D1::RectF(wx,bcy-bh2,fillX,bcy+bh2),bh2,bh2};
                                         rt->FillRoundedRectangle(rr, b.Get()); } }
                { const float tx2 = wx + (wx1-wx)*t;
                  const float tr = 8.0f * s;
                  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                  rt->CreateSolidColorBrush(
                      sel ? Tok::GoldBright(0.97f*ease) : Tok::GoldMid(0.60f*ease), b.GetAddressOf());
                  if (b) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx2,bcy),tr,tr), b.Get()); }
                if (sel) {
                    const float tx2 = wx + (wx1-wx)*t;
                    const float tr = 8.0f * s;
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> rb;
                    rt->CreateSolidColorBrush(Tok::GoldHi(0.55f*ease), rb.GetAddressOf());
                    if (rb) rt->DrawEllipse(
                        D2D1::Ellipse(D2D1::Point2F(tx2,bcy),tr+1.5f*s,tr+1.5f*s),
                        rb.Get(), 1.2f*s);
                }
                if (dwrite) {
                    wchar_t buf[32];
                    std::swprintf(buf, 32, L"%.2f%ls", *row.fTarget, row.unit ? row.unit : L"");
                    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
                    dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                        DWRITE_FONT_STRETCH_NORMAL, 11.0f*s, L"en-us", fmt.GetAddressOf());
                    if (fmt) {
                        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                        rt->CreateSolidColorBrush(
                            sel ? Tok::GoldHi(0.90f*ease) : Tok::ChromeMute(0.65f*ease),
                            b.GetAddressOf());
                        if (b) rt->DrawText(buf, static_cast<UINT32>(std::wcslen(buf)),
                            fmt.Get(), D2D1::RectF(wx1-60.0f*s, ry, wx1, ry+rh), b.Get());
                    }
                }
            } else if (row.type == RowType::BoolToggle && row.bTarget) {
                const float springVal = (flat >= 0 && flat < static_cast<int32_t>(m_toggleSprings.size()))
                    ? std::clamp(m_toggleSprings[static_cast<size_t>(flat)].value, 0.0f, 1.0f)
                    : (*row.bTarget ? 1.0f : 0.0f);
                const float tcx = (wx + wx1) * 0.5f;
                const float tcy = ry + rh * 0.5f;
                const float tw  = 42.0f * s;
                const float th  = 22.0f * s;
                { const D2D1_COLOR_F offCol = Tok::SurfaceSunken(0.88f * ease);
                  const D2D1_COLOR_F onCol  = Tok::GoldDeep(0.72f * ease);
                  const D2D1_COLOR_F trackCol = D2D1::ColorF(
                      offCol.r+(onCol.r-offCol.r)*springVal,
                      offCol.g+(onCol.g-offCol.g)*springVal,
                      offCol.b+(onCol.b-offCol.b)*springVal,
                      offCol.a+(onCol.a-offCol.a)*springVal);
                  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                  rt->CreateSolidColorBrush(trackCol, b.GetAddressOf());
                  if (b) { D2D1_ROUNDED_RECT rr{
                      D2D1::RectF(tcx-tw*0.5f,tcy-th*0.5f,tcx+tw*0.5f,tcy+th*0.5f),
                      th*0.5f, th*0.5f};
                      rt->FillRoundedRectangle(rr, b.Get()); } }
                if (sel) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                    rt->CreateSolidColorBrush(Tok::GoldMid(0.45f*ease), b.GetAddressOf());
                    if (b) { D2D1_ROUNDED_RECT rr{
                        D2D1::RectF(tcx-tw*0.5f,tcy-th*0.5f,tcx+tw*0.5f,tcy+th*0.5f),
                        th*0.5f, th*0.5f};
                        rt->DrawRoundedRectangle(rr, b.Get(), 1.5f*s); }
                }
                const float kr  = (th * 0.5f) - 2.5f*s;
                const float kx0 = tcx - tw*0.5f + kr + 2.5f*s;
                const float kx1 = tcx + tw*0.5f - kr - 2.5f*s;
                const float kcx = kx0 + (kx1 - kx0) * springVal;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> kb;
                rt->CreateSolidColorBrush(Tok::ChromeHi(0.97f * ease), kb.GetAddressOf());
                if (kb) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kcx,tcy),kr,kr), kb.Get());
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Hint bar
    // ---------------------------------------------------------------------------
    {
        const float hy  = py + totalH - hintBarH - padY * 0.55f;
        const float hcy = hy + hintBarH * 0.5f;
        const float fnt = 12.5f * s;
        float hx = px + padX;
        const float chipGap = 8.0f * s;
        DrawHintChip(rt, dwrite, hx, hcy, s, ease,
            L"LB / RB  tabs",
            Tok::GoldDeep(0.60f * ease), Tok::GoldHi(0.92f * ease), fnt);
        hx += 100.0f*s + chipGap;
        DrawHintChip(rt, dwrite, hx, hcy, s, ease,
            L"\u25CF  confirm",
            Tok::SurfaceRaised(0.90f * ease), Tok::ChromeMid(0.80f * ease), fnt);
        hx += 86.0f*s + chipGap;
        DrawHintChip(rt, dwrite, hx, hcy, s, ease,
            L"\u25C6  close",
            Tok::SurfaceRaised(0.90f * ease), Tok::ChromeMid(0.80f * ease), fnt);
        hx += 80.0f*s + chipGap;
        DrawHintChip(rt, dwrite, hx, hcy, s, ease,
            L"\u25B2  reset",
            Tok::SurfaceRaised(0.90f * ease), Tok::ChromeMute(0.65f * ease), fnt);
    }
}

} // namespace enjoystick::overlay
