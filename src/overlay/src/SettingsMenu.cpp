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

namespace enjoystick::overlay {

SettingsMenu::SettingsMenu(OnChangedCallback onChange)
    : m_onChange(std::move(onChange))
{ BuildRows(); }

// ---------------------------------------------------------------------------
// BuildRows
// ---------------------------------------------------------------------------
void SettingsMenu::BuildRows() {
    m_rows.clear();
    m_rows.reserve(16);

    m_rows.push_back({ L"Cursor",                  RowType::SectionHeader });
    m_rows.push_back({ L"Cursor speed",             RowType::FloatSlider, 1.0f,   20.0f,  0.5f,  &m_values.cursorSpeed,       nullptr,                    L" px/ms" });
    m_rows.push_back({ L"Curve exponent",           RowType::FloatSlider, 0.8f,   3.0f,   0.05f, &m_values.curveExponent,     nullptr,                    L""       });
    m_rows.push_back({ L"Acceleration ramp",        RowType::FloatSlider, 20.0f,  500.0f, 10.0f, &m_values.accelerationMs,    nullptr,                    L" ms"    });
    m_rows.push_back({ L"Right stick moves cursor", RowType::BoolToggle,  0,      0,      0,     nullptr,                     &m_values.useRightStick,    L""       });

    m_rows.push_back({ L"Scrolling",                RowType::SectionHeader });
    m_rows.push_back({ L"Scroll speed",             RowType::FloatSlider, 1.0f,   40.0f,  0.5f,  &m_values.scrollSpeed,       nullptr,                    L""       });
    m_rows.push_back({ L"Triggers as clicks",       RowType::BoolToggle,  0,      0,      0,     nullptr,                     &m_values.triggersAsClicks, L""       });

    m_rows.push_back({ L"Adaptive Speed",           RowType::SectionHeader });
    m_rows.push_back({ L"Adaptive speed",           RowType::BoolToggle,  0,      0,      0,     nullptr,                     &m_values.adaptiveSpeed,    L""       });
    m_rows.push_back({ L"Traversal time",           RowType::FloatSlider, 300.0f, 2000.f, 50.0f, &m_values.targetTraversalMs, nullptr,                    L" ms"    });
    m_rows.push_back({ L"DPI weight",               RowType::FloatSlider, 0.0f,   1.0f,   0.05f, &m_values.dpiWeight,         nullptr,                    L""       });

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

void SettingsMenu::OnRowChanged(int32_t newRow) {
    m_prevRow    = m_selectedRow;
    m_trailAlpha = 1.0f;
    m_selAnimT   = 0.0f;
    m_selectedRow = newRow;
    const int32_t total = static_cast<int32_t>(m_rows.size());
    if (m_selectedRow < m_scrollOffset) {
        m_scrollOffset = m_selectedRow;
    } else if (m_selectedRow >= m_scrollOffset + kVisibleRows) {
        m_scrollOffset = m_selectedRow - kVisibleRows + 1;
    }
    m_scrollOffset = std::max(0, std::min(m_scrollOffset, std::max(0, total - kVisibleRows)));
}

void SettingsMenu::Open(const Values& current) {
    m_values       = current;
    BuildRows();
    m_scrollOffset = 0;
    m_selectedRow  = NextInteractiveRow(-1, 1);
    m_state        = State::Opening;
    m_animProgress = 0.0f;
    m_repeatTimer  = 0.0f;
    m_prevRow      = -1;
    m_trailAlpha   = 0.0f;
    m_selAnimT     = 1.0f;

    m_stickNavActive    = false;
    m_stickNavCooldown  = 0.0f;
    m_stickNavHoldTime  = 0.0f;
    m_stickLxActive     = false;
    m_stickLxCooldown   = 0.0f;
    m_dpadVertHeld      = false;
    m_dpadVertTimer     = 0.0f;
    m_dpadHorzHeld      = false;
    m_dpadHorzTimer     = 0.0f;

    m_prevSouth = m_prevEast = m_prevNorth =
    m_prevDUp = m_prevDDown = m_prevDLeft = m_prevDRight = false;
}

void SettingsMenu::Close() {
    if (m_state == State::Hidden) return;
    m_state = State::Closing;

    m_stickNavActive    = false;
    m_stickNavCooldown  = 0.0f;
    m_stickNavHoldTime  = 0.0f;
    m_stickLxActive     = false;
    m_stickLxCooldown   = 0.0f;
    m_dpadVertHeld      = false;
    m_dpadVertTimer     = 0.0f;
    m_dpadHorzHeld      = false;
    m_dpadHorzTimer     = 0.0f;
}

bool SettingsMenu::IsOpen() const noexcept { return m_state != State::Hidden; }

void SettingsMenu::ResetToDefaults() {
    m_values = Values{};
    BuildRows();
    m_scrollOffset = 0;
    m_selectedRow = NextInteractiveRow(-1, 1);
    m_prevRow     = -1;
    m_trailAlpha  = 0.0f;
    m_selAnimT    = 1.0f;
    CommitChange();
}

void SettingsMenu::Update(const ControllerState& state, float dt) {
    UpdateAnimation(dt);
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

    // ---- DPad vertical: navigate rows
    // dUp = previous row (higher in list = smaller index)
    // dDown = next row (lower in list = larger index)
    {
        const bool dVert = dUp || dDown;
        if (dVert) {
            if (!m_dpadVertHeld) {
                m_dpadVertHeld  = true;
                m_dpadVertTimer = kSnapFirst;
                const int32_t next = NextInteractiveRow(m_selectedRow, dUp ? -1 : 1);
                if (next != m_selectedRow) OnRowChanged(next);
            } else {
                m_dpadVertTimer -= dt;
                if (m_dpadVertTimer <= 0.0f) {
                    m_dpadVertTimer = kSnapNext;
                    const int32_t next = NextInteractiveRow(m_selectedRow, dUp ? -1 : 1);
                    if (next != m_selectedRow) OnRowChanged(next);
                }
            }
        } else {
            m_dpadVertHeld  = false;
            m_dpadVertTimer = 0.0f;
        }
    }

    if (south && !m_prevSouth) {
        const auto& row = m_rows[static_cast<size_t>(m_selectedRow)];
        if (row.type == RowType::BoolToggle && row.bTarget) {
            *row.bTarget = !(*row.bTarget);
            CommitChange();
        }
    }

    // ---- DPad horizontal: adjust selected slider
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

    // ---- Left Stick Y: navigate rows
    // XInput convention: ly > 0 = stick pushed UP = go to PREVIOUS (smaller index) row
    // ly < 0 = stick pushed DOWN = go to NEXT (larger index) row
    {
        const float ly = state.leftStick.y;
        const float lx = state.leftStick.x;
        const bool  lyActive = std::abs(ly) > kNavDeadzone;
        const bool  lxActive = std::abs(lx) > 0.25f;

        if (lyActive && !m_stickLxActive) {
            // FIX: invert sign — ly > 0 means stick up = row index decreases
            const int dir = (ly > 0.0f) ? -1 : 1;
            if (!m_stickNavActive) {
                m_stickNavActive   = true;
                m_stickNavCooldown = kSnapFirst;
                m_stickNavHoldTime = 0.0f;
                const int32_t next = NextInteractiveRow(m_selectedRow, dir);
                if (next != m_selectedRow) OnRowChanged(next);
            } else {
                m_stickNavHoldTime += dt;
                m_stickNavCooldown -= dt;
                if (m_stickNavCooldown <= 0.0f) {
                    const float accelT   = std::max(0.0f,
                        (m_stickNavHoldTime - kNavAccelStart) / kNavAccelRange);
                    const float blend    = std::min(1.0f, accelT * accelT);
                    const float interval = kSnapNext + (kSnapFast - kSnapNext) * blend;
                    m_stickNavCooldown = interval;
                    const int32_t next = NextInteractiveRow(m_selectedRow, dir);
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
                const auto& row = m_rows[static_cast<size_t>(m_selectedRow)];
                if (row.type == RowType::FloatSlider && row.fTarget) {
                    *row.fTarget = std::clamp(
                        *row.fTarget + (lx > 0 ? 1.0f : -1.0f) * row.step,
                        row.min, row.max);
                    CommitChange();
                }
            } else {
                m_stickLxCooldown -= dt;
                if (m_stickLxCooldown <= 0.0f) {
                    m_stickLxCooldown = kSnapNext * 0.6f;
                    const auto& row = m_rows[static_cast<size_t>(m_selectedRow)];
                    if (row.type == RowType::FloatSlider && row.fTarget) {
                        *row.fTarget = std::clamp(
                            *row.fTarget + (lx > 0 ? 1.0f : -1.0f) * row.step,
                            row.min, row.max);
                        CommitChange();
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
    if (!IsInteractiveRow(m_selectedRow)) return;
    const auto& row = m_rows[static_cast<size_t>(m_selectedRow)];
    if (row.type != RowType::FloatSlider || !row.fTarget) return;
    const float step = repeat ? row.step * 1.5f : row.step;
    *row.fTarget = std::clamp(*row.fTarget + direction * step, row.min, row.max);
    CommitChange();
}

void SettingsMenu::CommitChange() { if (m_onChange) m_onChange(m_values); }

// ---------------------------------------------------------------------------
// Draw helpers
// ---------------------------------------------------------------------------
namespace {

// Heavier spring-bounce pop: A=0.22, k=14, w=22
// Produces a visible overshoot then settles, giving the "magnetised" feel
static float SelPopScale(float t) noexcept {
    if (t >= 1.0f) return 1.0f;
    const float A = 0.22f;
    const float k = 14.0f;
    const float w = 22.0f;
    return 1.0f + A * std::exp(-k * t) * std::cos(w * t);
}

static void DrawPanelChrome(
    ID2D1RenderTarget* rt, ID2D1Factory* fac,
    float px, float py, float pw, float ph, float r, float s, float ease) noexcept
{
    D2D1_ROUNDED_RECT rr{ D2D1::RectF(px, py, px+pw, py+ph), r, r };
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
    rt->CreateSolidColorBrush(Tok::GoldShadow(0.30f * ease), b.GetAddressOf());
    if (b) rt->DrawRoundedRectangle(rr, b.Get(), 1.8f);
    (void)fac; (void)s;
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

    if (dwrite && !m_dwriteEllipsis) {
        dwrite->CreateEllipsisTrimmingSign(
            [&]() -> IDWriteTextFormat* {
                IDWriteTextFormat* proto = nullptr;
                dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                    DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL, 13.0f * dpiScale,
                    L"en-us", &proto);
                return proto;
            }(),
            m_dwriteEllipsis.GetAddressOf());
    }

    Microsoft::WRL::ComPtr<ID2D1Factory> fac;
    rt->GetFactory(fac.GetAddressOf());

    const float s       = dpiScale;
    const float pw      = 580.0f * s;
    const float ph_row  = 44.0f  * s;
    const float ph_hdr  = 28.0f  * s;
    const float padX    = 28.0f  * s;
    const float padY    = 20.0f  * s;
    const float gap     =  4.0f  * s;
    const float cr      = 14.0f  * s;
    const float accentH =  3.0f  * s;
    const float hintBarH= 36.0f  * s;
    // Scrollbar stays fully inside the rounded panel
    // sbPad is measured from the inner edge of the rounding
    const float sbW     =  6.0f  * s;
    const float sbPad   =  7.0f  * s;  // right margin inside panel (was 5)
    const float sbX     = px_placeholder_set_below_in_draw_ctx_{}; // computed below

    const int32_t totalRows   = static_cast<int32_t>(m_rows.size());
    const int32_t numHeaders  = 4;
    const int32_t numData     = totalRows - numHeaders;
    const int32_t visData     = std::min(numData, kVisibleRows);
    const float   listH       = static_cast<float>(visData) * (ph_row + gap)
                              + static_cast<float>(numHeaders) * (ph_hdr + gap)
                              - gap;

    const float totalH = padY * 2.0f + accentH + hintBarH + listH;

    const float px = (screenW - pw) * 0.5f;
    const float py = (screenH - totalH) * 0.5f;
    // Scrollbar X: inset from right edge, clear of corner rounding
    const float sbXCalc = px + pw - sbW - sbPad - cr * 0.35f;

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
    if (fac) DrawPanelChrome(rt, fac.Get(), px, py, pw, totalH, cr, s, ease);

    // ---- Rows
    float ry = py + padY + accentH;
    int32_t dataRowsSeen = 0;
    for (int32_t i = 0; i < totalRows; ++i) {
        const auto& row = m_rows[static_cast<size_t>(i)];
        const bool  sel  = (i == m_selectedRow);
        const bool  prev = (i == m_prevRow);
        const float rh   = (row.type == RowType::SectionHeader) ? ph_hdr : ph_row;

        if (row.type != RowType::SectionHeader) {
            if (dataRowsSeen < m_scrollOffset || dataRowsSeen >= m_scrollOffset + kVisibleRows) {
                ++dataRowsSeen;
                continue;
            }
            ++dataRowsSeen;
        } else {
            int32_t nextData = dataRowsSeen;
            bool headerVisible = false;
            for (int32_t j = i + 1; j < totalRows; ++j) {
                if (m_rows[static_cast<size_t>(j)].type == RowType::SectionHeader) break;
                if (nextData >= m_scrollOffset && nextData < m_scrollOffset + kVisibleRows) {
                    headerVisible = true;
                    break;
                }
                ++nextData;
            }
            if (!headerVisible) continue;
        }

        if (row.type == RowType::SectionHeader) {
            if (dwrite && row.label) {
                Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
                dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                    DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL, 10.5f * s, L"en-us", fmt.GetAddressOf());
                if (fmt) {
                    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_FAR);
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                    rt->CreateSolidColorBrush(Tok::GoldMid(0.60f * ease), b.GetAddressOf());
                    if (b) rt->DrawText(row.label,
                        static_cast<UINT32>(std::wcslen(row.label)),
                        fmt.Get(),
                        D2D1::RectF(px+padX, ry, px+pw-padX, ry+rh), b.Get());
                }
            }
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(Tok::GoldDeep(0.18f * ease), b.GetAddressOf());
                if (b) rt->DrawLine(
                    D2D1::Point2F(px+padX, ry+rh-1.0f),
                    D2D1::Point2F(px+pw-padX, ry+rh-1.0f),
                    b.Get(), 0.8f);
            }
        } else {
            if (prev && m_trailAlpha > 0.0f) {
                const float ta = m_trailAlpha * m_trailAlpha * ease;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tb;
                rt->CreateSolidColorBrush(Tok::GoldMid(0.55f * ta), tb.GetAddressOf());
                if (tb) {
                    D2D1_ROUNDED_RECT rr{
                        D2D1::RectF(px+8.0f*s, ry+1.0f*s, px+pw-8.0f*s, ry+rh-1.0f*s),
                        7.0f*s, 7.0f*s };
                    rt->FillRoundedRectangle(rr, tb.Get());
                }
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> gb;
                rt->CreateSolidColorBrush(Tok::GoldHi(0.65f * ta), gb.GetAddressOf());
                if (gb) {
                    D2D1_ROUNDED_RECT rr{
                        D2D1::RectF(px+8.0f*s, ry+1.0f*s, px+pw-8.0f*s, ry+rh-1.0f*s),
                        7.0f*s, 7.0f*s };
                    rt->DrawRoundedRectangle(rr, gb.Get(), 1.2f*s);
                }
            }

            if (sel) {
                // Spring-bounce pop scale
                const float popSc  = SelPopScale(m_selAnimT);
                const float inset  = (1.0f - popSc) * ph_row * 0.5f;
                const float selTop = ry + 1.0f*s + inset;
                const float selBot = ry + rh - 1.0f*s - inset;

                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(Tok::SurfaceRaised(0.80f * ease), b.GetAddressOf());
                if (b) { D2D1_ROUNDED_RECT rr{
                    D2D1::RectF(px+8.0f*s, selTop, px+pw-8.0f*s, selBot),
                    7.0f*s, 7.0f*s};
                    rt->FillRoundedRectangle(rr, b.Get()); }
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ab;
                rt->CreateSolidColorBrush(Tok::GoldHi(0.85f * ease), ab.GetAddressOf());
                if (ab) { D2D1_ROUNDED_RECT rr{
                    D2D1::RectF(px+8.0f*s, ry+6.0f*s, px+11.5f*s, ry+rh-6.0f*s),
                    1.5f*s, 1.5f*s};
                    rt->FillRoundedRectangle(rr, ab.Get()); }
            }
            if (dwrite && row.label) {
                Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
                dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                    DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL, 13.5f * s, L"en-us", fmt.GetAddressOf());
                if (fmt) {
                    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    Microsoft::WRL::ComPtr<IDWriteTextLayout> lay;
                    dwrite->CreateTextLayout(row.label,
                        static_cast<UINT32>(std::wcslen(row.label)),
                        fmt.Get(), pw * 0.5f - padX - 8.0f * s, ph_row,
                        lay.GetAddressOf());
                    if (lay) {
                        lay->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                        if (m_dwriteEllipsis) {
                            DWRITE_TRIMMING trimming{};
                            trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
                            trimming.delimiter   = 0;
                            trimming.delimiterCount = 0;
                            lay->SetTrimming(&trimming, m_dwriteEllipsis.Get());
                        }
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                        rt->CreateSolidColorBrush(
                            sel ? Tok::GoldHi(0.96f*ease) : Tok::ChromeHi(0.82f*ease),
                            b.GetAddressOf());
                        if (b) rt->DrawTextLayout(
                            D2D1::Point2F(px+padX+6.0f*s, ry + (ph_row - 13.5f*s*1.4f)*0.5f),
                            lay.Get(), b.Get());
                    }
                }
            }
            // ---- Value widget (right half)
            // wx1 accounts for scrollbar space: subtract sbW + sbPad + corner clearance
            const float wx  = px + pw * 0.5f;
            const float wx1 = sbXCalc - 4.0f * s;

            if (row.type == RowType::FloatSlider && row.fTarget) {
                const float t   = std::clamp((*row.fTarget - row.min) / (row.max - row.min + 0.0001f), 0.0f, 1.0f);
                const float bcy = ry + rh * 0.5f;
                const float bh2 = 5.0f * s;
                {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                    rt->CreateSolidColorBrush(Tok::SurfaceSunken(0.90f * ease), b.GetAddressOf());
                    if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(wx,bcy-bh2,wx1,bcy+bh2),bh2,bh2};
                             rt->FillRoundedRectangle(rr, b.Get()); }
                }
                {
                    const float fillX = wx + (wx1-wx)*t;
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                    rt->CreateSolidColorBrush(
                        sel ? Tok::GoldMid(0.88f*ease) : Tok::GoldDeep(0.60f*ease),
                        b.GetAddressOf());
                    if (b && fillX > wx) { D2D1_ROUNDED_RECT rr{D2D1::RectF(wx,bcy-bh2,fillX,bcy+bh2),bh2,bh2};
                                           rt->FillRoundedRectangle(rr, b.Get()); }
                }
                {
                    const float tx2 = wx + (wx1-wx)*t;
                    const float tr = 8.0f * s;
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                    rt->CreateSolidColorBrush(
                        sel ? Tok::GoldBright(0.97f*ease) : Tok::GoldMid(0.60f*ease),
                        b.GetAddressOf());
                    if (b) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx2,bcy),tr,tr), b.Get());
                }
                if (sel) {
                    const float tx2 = wx + (wx1-wx)*t;
                    const float tr = 8.0f * s;
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> rb;
                    rt->CreateSolidColorBrush(Tok::GoldHi(0.55f*ease), rb.GetAddressOf());
                    if (rb) rt->DrawEllipse(
                        D2D1::Ellipse(D2D1::Point2F(tx2,bcy), tr+1.5f*s, tr+1.5f*s),
                        rb.Get(), 1.2f*s);
                }
                if (dwrite && row.fTarget) {
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
                const bool  on  = *row.bTarget;
                const float tcx = (wx + wx1) * 0.5f;
                const float tcy = ry + rh * 0.5f;
                const float tw  = 42.0f * s;
                const float th  = 22.0f * s;
                {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                    rt->CreateSolidColorBrush(
                        on ? Tok::GoldDeep(0.72f*ease) : Tok::SurfaceSunken(0.88f*ease),
                        b.GetAddressOf());
                    if (b) { D2D1_ROUNDED_RECT rr{
                        D2D1::RectF(tcx-tw*0.5f,tcy-th*0.5f,tcx+tw*0.5f,tcy+th*0.5f),
                        th*0.5f, th*0.5f};
                        rt->FillRoundedRectangle(rr, b.Get()); }
                }
                if (sel) {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                    rt->CreateSolidColorBrush(Tok::GoldMid(0.45f*ease), b.GetAddressOf());
                    if (b) { D2D1_ROUNDED_RECT rr{
                        D2D1::RectF(tcx-tw*0.5f,tcy-th*0.5f,tcx+tw*0.5f,tcy+th*0.5f),
                        th*0.5f, th*0.5f};
                        rt->DrawRoundedRectangle(rr, b.Get(), 1.1f*s); }
                }
                {
                    const float tx2 = on ? tcx+tw*0.5f-th*0.5f : tcx-tw*0.5f+th*0.5f;
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                    rt->CreateSolidColorBrush(
                        on ? Tok::GoldBright(0.97f*ease) : Tok::ChromeMute(0.60f*ease),
                        b.GetAddressOf());
                    if (b) rt->FillEllipse(
                        D2D1::Ellipse(D2D1::Point2F(tx2,tcy), th*0.44f, th*0.44f), b.Get());
                }
            }
        }
        ry += rh + gap;
    }

    // ---- Scrollbar (fully inside panel, clear of rounded corners)
    {
        const int32_t numInteractive = numData;
        if (numInteractive > kVisibleRows) {
            const float listTop2  = py + padY + accentH;
            const float listBot2  = listTop2 + listH;
            // Inset track top/bottom by cr*0.5 so thumb stays clear of corner rounding
            const float trackTop  = listTop2 + cr * 0.5f;
            const float trackBot  = listBot2 - cr * 0.5f;
            const float trackH    = trackBot - trackTop;

            // Track
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(Tok::SurfaceSunken(0.60f * ease), b.GetAddressOf());
                if (b) { D2D1_ROUNDED_RECT rr{ D2D1::RectF(sbXCalc, trackTop, sbXCalc+sbW, trackBot),
                    sbW*0.5f, sbW*0.5f };
                    rt->FillRoundedRectangle(rr, b.Get()); }
            }

            // Thumb
            const float maxOffset  = static_cast<float>(numInteractive - kVisibleRows);
            const float thumbRatio = static_cast<float>(kVisibleRows) / static_cast<float>(numInteractive);
            const float thumbH     = std::max(24.0f * s, trackH * thumbRatio);
            const float scrollT    = (maxOffset > 0.0f)
                ? static_cast<float>(m_scrollOffset) / maxOffset
                : 0.0f;
            const float thumbTop   = trackTop + scrollT * (trackH - thumbH);
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(Tok::GoldMid(0.65f * ease), b.GetAddressOf());
                if (b) { D2D1_ROUNDED_RECT rr{ D2D1::RectF(sbXCalc, thumbTop, sbXCalc+sbW, thumbTop+thumbH),
                    sbW*0.5f, sbW*0.5f };
                    rt->FillRoundedRectangle(rr, b.Get()); }
            }
        }
    }

    // ---- Hint bar
    if (dwrite) {
        const float hintY  = py + totalH - hintBarH;
        const float hintCY = hintY + hintBarH * 0.5f;
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(Tok::GoldDeep(0.14f * ease), b.GetAddressOf());
            if (b) rt->DrawLine(
                D2D1::Point2F(px + padX, hintY),
                D2D1::Point2F(px + pw - padX, hintY),
                b.Get(), 0.6f);
        }
        const float fnt    = 11.0f * s;
        const float chipGap = 10.0f * s;
        float hx = px + padX;

        DrawHintChip(rt, dwrite, hx, hintCY, s, ease,
            L"\u25CF  Toggle",
            Tok::GoldDeep(0.70f * ease), Tok::GoldHi(0.92f * ease), fnt);
        hx += 90.0f * s + chipGap;

        DrawHintChip(rt, dwrite, hx, hintCY, s, ease,
            L"\u25C6  Close",
            Tok::SurfaceRaised(0.90f * ease), Tok::ChromeMid(0.80f * ease), fnt);
        hx += 82.0f * s + chipGap;

        DrawHintChip(rt, dwrite, hx, hintCY, s, ease,
            L"\u25B2  Reset defaults",
            Tok::SurfaceRaised(0.90f * ease), Tok::ChromeMid(0.80f * ease), fnt);
        hx += 130.0f * s + chipGap;

        DrawHintChip(rt, dwrite, hx, hintCY, s, ease,
            L"\u25C4\u25BA  Adjust value",
            Tok::GoldMid(0.55f * ease), Tok::GoldBright(0.95f * ease), fnt);
    }
    (void)kPif;
}

} // namespace enjoystick::overlay
