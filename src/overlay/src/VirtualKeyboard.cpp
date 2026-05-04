#include <enjoystick/overlay/VirtualKeyboard.hpp>
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

// ---------------------------------------------------------------------------
// Layout constants — TV / couch viewing distance (1-2 m)
// ---------------------------------------------------------------------------
static constexpr float kKeyW_base   = 72.0f;
static constexpr float kKeyH_base   = 68.0f;
static constexpr float kGap_base    =  7.0f;
static constexpr float kCorner_base = 10.0f;
static constexpr float kPadX_base   = 28.0f;
static constexpr float kPadY_base   = 22.0f;
static constexpr float kTbH_base    = 60.0f;
static constexpr float kHintH_base  = 30.0f;
static constexpr float kFKey_base   = 20.0f;
static constexpr float kFSpec_base  = 18.0f;
static constexpr float kFText_base  = 18.0f;
static constexpr float kFHint_base  = 13.0f;
static constexpr float kFBadge_base = 12.0f;

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
void VirtualKeyboard::BuildLayout() {
    m_rows.clear();

    // Row 0 — digits + backspace
    m_rows.push_back({
        {L"1",L"!",L"\u00B1"}, {L"2",L"@",L"\u00B2"}, {L"3",L"#",L"\u00B3"},
        {L"4",L"$",L"\u00A3"}, {L"5",L"%",L"\u20AC"}, {L"6",L"^",L"\u00B6"},
        {L"7",L"&",L"\u2022"}, {L"8",L"*",L"\u00D7"}, {L"9",L"(",L"\u2018"},
        {L"0",L")",L"\u2019"},
        {L"\u232B",L"\u232B",L"\u232B", 1.5f, true},
    });
    // Row 1 — QWERTY
    m_rows.push_back({
        {L"q",L"Q",L"-"}, {L"w",L"W",L"_"}, {L"e",L"E",L"="},
        {L"r",L"R",L"+"}, {L"t",L"T",L"["}, {L"y",L"Y",L"]"},
        {L"u",L"U",L"{"}, {L"i",L"I",L"}"}, {L"o",L"O",L"\\\\"},
        {L"p",L"P",L"|"},
    });
    // Row 2 — ASDF
    m_rows.push_back({
        {L"a",L"A",L"~"}, {L"s",L"S",L"`"}, {L"d",L"D",L"<"},
        {L"f",L"F",L">"}, {L"g",L"G",L","}, {L"h",L"H",L"."},
        {L"j",L"J",L"\u2026"}, {L"k",L"K",L"\u2014"}, {L"l",L"L",L":"},
        {L"\u23CE",L"\u23CE",L"\u23CE", 1.5f, true},
    });
    // Row 3 — ZXCV + space
    m_rows.push_back({
        {L"\u21E7",L"\u21E7",L"\u21E7", 1.5f, true},
        {L"z",L"Z",L"/"}, {L"x",L"X",L"?"}, {L"c",L"C",L"\'"},
        {L"v",L"V",L"\""}, {L"b",L"B",L" "}, {L"n",L"N",L"\u2122"},
        {L"m",L"M",L"\u00AE"}, {L".",L".",L","},
        {L"\u2423",L"\u2423",L"\u2423", 4.0f, true},
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
    if (m_layer == Layer::Sym)  return k.symLabel;
    if (m_shift || m_caps)      return k.shiftLabel;
    return k.label;
}
void VirtualKeyboard::NavigateTo(int32_t row, int32_t col) {
    const int32_t nr = static_cast<int32_t>(m_rows.size());
    row = (row % nr + nr) % nr;
    const int32_t n = RowKeyCount(row);
    col = (n > 0) ? (col % n + n) % n : 0;
    m_row = row; m_col = col;
}

// Compute the pixel centre of key [row][col] in the current layout.
// Returns {-9999,-9999} if out of range.
Vec2 VirtualKeyboard::KeyCentrePixel(int32_t row, int32_t col,
                                      float dpiScale, float screenW, float) const noexcept
{
    const float s       = dpiScale;
    const float kKeyW   = kKeyW_base * s;
    const float kKeyH   = kKeyH_base * s;
    const float kGap    = kGap_base  * s;
    const float kPadX   = kPadX_base * s;
    const float kPadY   = kPadY_base * s;
    const float kTbH    = kTbH_base  * s;

    // Panel width (same calc as Draw)
    float maxRowW = 0.0f;
    for (const auto& r : m_rows) {
        float rw = -kGap;
        for (const auto& k : r) rw += k.widthMul * kKeyW + kGap;
        if (rw > maxRowW) maxRowW = rw;
    }
    const float kPanelW = maxRowW + kPadX * 2.0f;
    const float panelX  = (screenW - kPanelW) * 0.5f;
    const float keysTop = 0.0f + kPadY + kTbH + 8.0f * s; // relative to panelY — absolute unused here

    if (row < 0 || row >= static_cast<int32_t>(m_rows.size())) return {-9999.0f,-9999.0f};
    const auto& rrow = m_rows[static_cast<size_t>(row)];
    if (col < 0 || col >= static_cast<int32_t>(rrow.size())) return {-9999.0f,-9999.0f};

    // Row X offset (centred)
    float rowW = -kGap;
    for (const auto& k : rrow) rowW += k.widthMul * kKeyW + kGap;
    float rx = panelX + (kPanelW - rowW) * 0.5f;
    for (int32_t c = 0; c < col; ++c)
        rx += rrow[static_cast<size_t>(c)].widthMul * kKeyW + kGap;
    const float kw = rrow[static_cast<size_t>(col)].widthMul * kKeyW;
    const float ry = keysTop + static_cast<float>(row) * (kKeyH + kGap);
    return { rx + kw * 0.5f, ry + kKeyH * 0.5f };
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void VirtualKeyboard::Open(const std::wstring& seed) {
    if (m_state == State::Visible || m_state == State::Opening) return;
    BuildLayout();
    m_text          = seed;
    m_row           = 3; m_col = 9; // space key
    m_layer         = Layer::Alpha;
    m_shift         = false; m_caps = false;
    m_glowPhase     = 0.0f;
    m_state         = State::Opening;
    m_stickCooldown = 0.0f; m_stickActive = false;
    m_prevSouth = m_prevEast = m_prevWest = m_prevNorth =
    m_prevLB    = m_prevRB  = m_prevLS   = false;

    // Spring: panel starts far below screen, springs up
    m_panelSpring.stiffness = 320.0f;
    m_panelSpring.damping   = 24.0f;
    m_panelSpring.Snap(0.0f);      // 0 = fully hidden
    m_panelSpring.SetTarget(1.0f); // 1 = fully visible

    // Cursor highlight spring: snap to current key, no slide yet
    m_cursorSpringX.stiffness = 480.0f; m_cursorSpringX.damping = 26.0f;
    m_cursorSpringY.stiffness = 480.0f; m_cursorSpringY.damping = 26.0f;
    m_cursorScaleSpring.stiffness = 600.0f; m_cursorScaleSpring.damping = 28.0f;
    m_cursorScaleSpring.Snap(1.0f);
}
void VirtualKeyboard::Close() {
    if (m_state == State::Hidden || m_state == State::Closing) return;
    m_state = State::Closing;
    m_panelSpring.SetTarget(0.0f);
}
bool VirtualKeyboard::IsOpen() const noexcept {
    return m_state != State::Hidden;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
void VirtualKeyboard::Update(const ControllerState& state, float dt) {
    // Advance panel spring
    m_panelSpring.Step(dt);
    const float panelVal = std::max(0.0f, std::min(1.0f, m_panelSpring.value));

    if (m_state == State::Opening) {
        if (m_panelSpring.IsSettled(0.01f) || panelVal >= 0.999f)
            m_state = State::Visible;
    } else if (m_state == State::Closing) {
        if (m_panelSpring.IsSettled(0.01f) || panelVal <= 0.001f) {
            m_state = State::Hidden;
            return;
        }
    }
    if (m_state == State::Hidden) return;

    m_glowPhase += dt * 3.0f;
    if (m_glowPhase > 2.0f * kPi) m_glowPhase -= 2.0f * kPi;

    // Advance cursor springs
    m_cursorSpringX.Step(dt);
    m_cursorSpringY.Step(dt);
    m_cursorScaleSpring.Step(dt);

    const bool south = HasButton(state.buttons, Button::South);
    const bool east  = HasButton(state.buttons, Button::East);
    const bool west  = HasButton(state.buttons, Button::West);
    const bool north = HasButton(state.buttons, Button::North);
    const bool lb    = HasButton(state.buttons, Button::LB);
    const bool rb    = HasButton(state.buttons, Button::RB);
    const bool ls    = HasButton(state.buttons, Button::LS);

    if (east  && !m_prevEast)  { Close(); goto done; }
    if (north && !m_prevNorth) { if (m_onSubmit) m_onSubmit(m_text); Close(); goto done; }
    if (west  && !m_prevWest)  { if (!m_text.empty()) m_text.pop_back(); goto done; }
    if (ls    && !m_prevLS)    { m_caps = !m_caps; m_shift = false; goto done; }
    if (lb    && !m_prevLB)    { m_layer = (m_layer == Layer::Alpha) ? Layer::Sym : Layer::Alpha; goto done; }
    if (rb    && !m_prevRB)    { m_layer = Layer::Alpha; goto done; }
    if (south && !m_prevSouth) {
        if (const Key* k = CurrentKey()) {
            TypeKey(*k);
            // Scale pop on keypress
            m_cursorScaleSpring.value = 1.18f;
            m_cursorScaleSpring.velocity = 0.0f;
            m_cursorScaleSpring.SetTarget(1.0f);
        }
        goto done;
    }

    {
        const float lx = state.leftStick.x;
        const float ly = state.leftStick.y;
        const float dz = 0.35f;
        const bool  hx = std::abs(lx) > dz;
        const bool  hy = std::abs(ly) > dz;
        if (hx || hy) {
            if (!m_stickActive) {
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
        } else { m_stickActive = false; m_stickCooldown = 0.0f; }
    }

done:
    m_prevSouth = south; m_prevEast  = east;
    m_prevWest  = west;  m_prevNorth = north;
    m_prevLB    = lb;    m_prevRB    = rb;
    m_prevLS    = ls;
}

void VirtualKeyboard::TypeKey(const Key& k) {
    if (k.isSpecial) {
        if (k.label == L"\u232B") { if (!m_text.empty()) m_text.pop_back(); return; }
        if (k.label == L"\u23CE") { m_text += L'\n'; if (m_onChar) m_onChar(L'\n'); return; }
        if (k.label == L"\u21E7") { m_shift = !m_shift; return; }
        if (k.label == L"\u2423") { m_text += L' '; if (m_onChar) m_onChar(L' '); return; }
        return;
    }
    const std::wstring d = KeyDisplay(k);
    if (d.size() == 1) {
        m_text += d[0];
        if (m_onChar) m_onChar(d[0]);
    }
    if (m_shift && !m_caps) m_shift = false;
}

// ---------------------------------------------------------------------------
// Draw helpers
// ---------------------------------------------------------------------------
namespace {

static void DrawArcSpecular(
    ID2D1RenderTarget* rt,
    ID2D1Factory*      fac,
    float cx, float cy, float rx, float ry,
    float alpha) noexcept
{
    if (!rt || !fac || rx < 2.0f || ry < 2.0f || alpha < 0.004f) return;
    const float aFrom = -kPi * 0.84f;
    const float aTo   = -kPi * 0.16f;
    const float arx   = rx - 1.5f;
    const float ary   = ry - 1.5f;
    if (arx <= 0.0f || ary <= 0.0f) return;

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> g;
    fac->CreatePathGeometry(g.GetAddressOf());
    if (!g) return;
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> s;
    g->Open(s.GetAddressOf());
    if (!s) return;
    s->BeginFigure(
        D2D1::Point2F(cx + arx * std::cos(aFrom), cy + ary * std::sin(aFrom)),
        D2D1_FIGURE_BEGIN_HOLLOW);
    D2D1_ARC_SEGMENT arc{};
    arc.point          = D2D1::Point2F(cx + arx * std::cos(aTo), cy + ary * std::sin(aTo));
    arc.size           = D2D1::SizeF(arx, ary);
    arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
    arc.arcSize        = D2D1_ARC_SIZE_SMALL;
    s->AddArc(arc);
    s->EndFigure(D2D1_FIGURE_END_OPEN);
    s->Close();
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
    rt->CreateSolidColorBrush(Tok::White(alpha), b.GetAddressOf());
    if (b) rt->DrawGeometry(g.Get(), b.Get(), 0.9f);
}

static void DrawPanelChrome(
    ID2D1RenderTarget* rt,
    float px, float py, float pw, float ph,
    float r, float s, float ease) noexcept
{
    { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
      rt->CreateSolidColorBrush(Tok::GoldShadow(0.30f * ease), b.GetAddressOf());
      if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(px,py,px+pw,py+ph),r,r};
               rt->DrawRoundedRectangle(rr, b.Get(), 1.8f); } }
    { const float d = 0.5f;
      Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
      rt->CreateSolidColorBrush(Tok::InkLine(0.90f * ease), b.GetAddressOf());
      if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(px+d,py+d,px+pw-d,py+ph-d),r-d,r-d};
               rt->DrawRoundedRectangle(rr, b.Get(), 0.65f); } }
    { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
      rt->CreateSolidColorBrush(Tok::White(0.058f * ease), b.GetAddressOf());
      if (b) rt->DrawLine(
          D2D1::Point2F(px + r,          py + 1.2f * s),
          D2D1::Point2F(px + pw * 0.40f, py + 1.2f * s),
          b.Get(), 0.9f); }
    (void)s;
}

} // anon

// ---------------------------------------------------------------------------
// Draw — Futurist Glamour v4 (spring cursor highlight)
// ---------------------------------------------------------------------------
void VirtualKeyboard::Draw(
    void*  renderTargetPtr,
    void*  dwriteFactoryPtr,
    float  dpiScale,
    float  screenW,
    float  screenH) const
{
    if (m_state == State::Hidden) return;
    const float panelVal = std::max(0.0f, std::min(1.0f, m_panelSpring.value));
    if (panelVal <= 0.001f) return;
    if (!renderTargetPtr || !dwriteFactoryPtr) return;

    auto* rt     = static_cast<ID2D1RenderTarget*>(renderTargetPtr);
    auto* dwrite = static_cast<IDWriteFactory*>(dwriteFactoryPtr);

    Microsoft::WRL::ComPtr<ID2D1Factory> fac;
    rt->GetFactory(fac.GetAddressOf());

    const float s    = dpiScale;
    // Cubic ease-out on panel spring value for alpha/position
    const float ease = 1.0f - std::pow(1.0f - panelVal, 3.0f);

    // ---- Layout -------------------------------------------------------------
    const float kKeyW   = kKeyW_base   * s;
    const float kKeyH   = kKeyH_base   * s;
    const float kGap    = kGap_base    * s;
    const float kCorner = kCorner_base * s;
    const float kPadX   = kPadX_base   * s;
    const float kPadY   = kPadY_base   * s;
    const float kTbH    = kTbH_base    * s;
    const float kHintH  = kHintH_base  * s;

    float maxRowW = 0.0f;
    for (const auto& row : m_rows) {
        float rw = -kGap;
        for (const auto& k : row) rw += k.widthMul * kKeyW + kGap;
        if (rw > maxRowW) maxRowW = rw;
    }
    const float kPanelW = maxRowW + kPadX * 2.0f;
    const float kPanelH = kPadY + kTbH + 8.0f*s
                        + static_cast<float>(m_rows.size()) * (kKeyH + kGap) - kGap
                        + kHintH + kPadY;

    // Spring-driven slide-up: panelVal=0 → fully off-screen below, panelVal=1 → target position
    const float targetY = screenH - kPanelH - 24.0f * s;
    const float panelY  = targetY + (1.0f - panelVal) * (screenH - targetY + 20.0f * s);
    const float panelX  = (screenW - kPanelW) * 0.5f;
    const float panelR  = 16.0f * s;

    // ---- Scrim
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::Scrim(0.44f * ease), b.GetAddressOf());
        if (b) rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, screenW, screenH), b.Get());
    }

    // ---- Panel fill
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceGlass(0.97f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(panelX,panelY,panelX+kPanelW,panelY+kPanelH),panelR,panelR};
                 rt->FillRoundedRectangle(rr, b.Get()); }
    }
    DrawPanelChrome(rt, panelX, panelY, kPanelW, kPanelH, panelR, s, ease);

    // ---- Text bar -----------------------------------------------------------
    const float tbX0 = panelX + kPadX;
    const float tbX1 = panelX + kPanelW - kPadX;
    const float tbY  = panelY + kPadY;
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceSunken(0.95f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(tbX0,tbY,tbX1,tbY+kTbH),8.0f*s,8.0f*s};
                 rt->FillRoundedRectangle(rr, b.Get()); }
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldDeep(0.65f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(tbX0,tbY,tbX1,tbY+kTbH),8.0f*s,8.0f*s};
                 rt->DrawRoundedRectangle(rr, b.Get(), 1.2f); }
    }
    if (dwrite) {
        const std::wstring display = m_text.size() > 48
            ? L"\u2026" + m_text.substr(m_text.size() - 47) : m_text;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
        dwrite->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, kFText_base * s, L"en-us", fmt.GetAddressOf());
        if (fmt) {
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(Tok::ChromeHi(0.94f * ease), b.GetAddressOf());
            if (b) rt->DrawText(display.c_str(), static_cast<UINT32>(display.size()),
                fmt.Get(), D2D1::RectF(tbX0 + 14.0f*s, tbY, tbX1 - 14.0f*s, tbY + kTbH), b.Get());
        }
    }
    // Blinking cursor
    {
        const float blink  = 0.5f + 0.5f * std::sin(m_glowPhase * 2.0f);
        const float textW  = std::min(
            static_cast<float>(m_text.size()) * 10.4f * s,
            tbX1 - tbX0 - 28.0f * s);
        const float cursorX = tbX0 + 14.0f * s + textW;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldHi(blink * ease), b.GetAddressOf());
        if (b) rt->DrawLine(
            D2D1::Point2F(cursorX, tbY + 10.0f * s),
            D2D1::Point2F(cursorX, tbY + kTbH - 10.0f * s),
            b.Get(), 2.0f * s);
    }

    // ---- Hint bar -----------------------------------------------------------
    {
        const float hy = panelY + kPanelH - kHintH - kPadY * 0.5f;
        if (dwrite) {
            if (m_layer == Layer::Sym || m_caps) {
                const wchar_t* badge = (m_layer == Layer::Sym) ? L"SYM" : L"CAPS";
                const float bw = 54.0f*s, bh = 20.0f*s;
                const float bx = panelX + kPanelW - kPadX - bw;
                const float by = hy + (kHintH - bh) * 0.5f;
                {
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fb;
                    rt->CreateSolidColorBrush(
                        (m_layer == Layer::Sym) ? Tok::GoldDeep(0.85f*ease) : Tok::AmberWarm(0.75f*ease),
                        fb.GetAddressOf());
                    if (fb) { D2D1_ROUNDED_RECT rr{D2D1::RectF(bx,by,bx+bw,by+bh),5.0f*s,5.0f*s};
                              rt->FillRoundedRectangle(rr, fb.Get()); }
                }
                Microsoft::WRL::ComPtr<IDWriteTextFormat> bf;
                dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                    DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                    kFBadge_base * s, L"en-us", bf.GetAddressOf());
                if (bf) {
                    bf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    bf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bb;
                    rt->CreateSolidColorBrush(Tok::GoldAccent(0.97f*ease), bb.GetAddressOf());
                    if (bb) rt->DrawText(badge, static_cast<UINT32>(std::wcslen(badge)),
                        bf.Get(), D2D1::RectF(bx,by,bx+bw,by+bh), bb.Get());
                }
            }
            const wchar_t* legend = (m_layer == Layer::Sym)
                ? L"[RB] Alpha   [X] \u232B   [Y] Submit   [B] Close"
                : L"[LB] Sym   [L3] Caps   [X] \u232B   [Y] Submit   [B] Close";
            Microsoft::WRL::ComPtr<IDWriteTextFormat> lf;
            dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                kFHint_base * s, L"en-us", lf.GetAddressOf());
            if (lf) {
                lf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> lb;
                rt->CreateSolidColorBrush(Tok::ChromeMute(0.65f*ease), lb.GetAddressOf());
                if (lb) rt->DrawText(legend, static_cast<UINT32>(std::wcslen(legend)),
                    lf.Get(), D2D1::RectF(panelX+kPadX, hy, panelX+kPanelW-kPadX, hy+kHintH), lb.Get());
            }
        }
    }

    // ---- Keys ---------------------------------------------------------------
    const float keysTop     = panelY + kPadY + kTbH + 8.0f * s;
    const float glowBreathe = 0.5f + 0.5f * std::sin(m_glowPhase);

    // Spring cursor glow position (spring tracks key centre in screen space)
    // On first draw, snap cursor spring to current key centre
    const Vec2 selCentre = KeyCentrePixel(m_row, m_col, s, screenW, screenH);
    const float cursorGlowX = m_cursorSpringX.value;
    const float cursorGlowY = m_cursorSpringY.value + panelY; // relative-to-panel offset

    // Update spring target every frame (mutable in const Draw via mutable springs)
    m_cursorSpringX.target = selCentre.x;
    m_cursorSpringY.target = selCentre.y; // panel-relative Y
    // If spring not initialised yet, snap it
    if (m_cursorSpringX.value == 0.0f && m_cursorSpringY.value == 0.0f) {
        m_cursorSpringX.Snap(selCentre.x);
        m_cursorSpringY.Snap(selCentre.y);
    }

    // Draw the smooth cursor glow UNDER all keys (so it appears behind the key caps)
    {
        const float scl  = m_cursorScaleSpring.value;
        const float gR   = kKeyH * 0.56f * scl;
        const float gW   = kKeyW * 0.56f * scl * (m_col >= 0 && m_col < RowKeyCount(m_row)
            ? m_rows[static_cast<size_t>(m_row)][static_cast<size_t>(m_col)].widthMul : 1.0f);
        // Outer glow halo
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> g;
            rt->CreateSolidColorBrush(
                Tok::GoldGlow((0.28f + 0.12f * glowBreathe) * ease), g.GetAddressOf());
            if (g) rt->DrawEllipse(
                D2D1::Ellipse(D2D1::Point2F(cursorGlowX, cursorGlowY),
                              gW * 1.20f, gR * 1.20f), g.Get(), 7.0f * s);
        }
        // Inner tight ring
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> g;
            rt->CreateSolidColorBrush(Tok::GoldGlow(0.14f * ease), g.GetAddressOf());
            if (g) rt->DrawEllipse(
                D2D1::Ellipse(D2D1::Point2F(cursorGlowX, cursorGlowY),
                              gW * 0.88f, gR * 0.88f), g.Get(), 2.5f * s);
        }
    }

    for (int32_t ri = 0; ri < static_cast<int32_t>(m_rows.size()); ++ri) {
        const auto& row = m_rows[static_cast<size_t>(ri)];
        const float ry  = keysTop + static_cast<float>(ri) * (kKeyH + kGap);

        float rowW = -kGap;
        for (const auto& k : row) rowW += k.widthMul * kKeyW + kGap;
        float rx = panelX + (kPanelW - rowW) * 0.5f;

        for (int32_t ci = 0; ci < static_cast<int32_t>(row.size()); ++ci) {
            const auto& k   = row[static_cast<size_t>(ci)];
            const float kw  = k.widthMul * kKeyW;
            const bool  sel = (ri == m_row && ci == m_col);
            const float kCx = rx + kw * 0.5f;
            const float kCy = ry + kKeyH * 0.5f;
            const float scl = sel ? m_cursorScaleSpring.value : 1.0f;
            const float scaledKW = kw  * scl;
            const float scaledKH = kKeyH * scl;
            const float sRx = kCx - scaledKW * 0.5f;
            const float sRy = kCy - scaledKH * 0.5f;

            // Keycap outer fill
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(
                    sel ? Tok::SurfaceBase(0.97f*ease) : Tok::SurfaceSunken(0.84f*ease),
                    b.GetAddressOf());
                if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(sRx,sRy,sRx+scaledKW,sRy+scaledKH),kCorner*scl,kCorner*scl};
                         rt->FillRoundedRectangle(rr, b.Get()); }
            }
            // Keycap inner face (raised plane, inset)
            {
                const float ins = 2.8f * s;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(
                    sel ? Tok::SurfaceRaised(0.90f*ease) : Tok::SurfaceBase(0.74f*ease),
                    b.GetAddressOf());
                if (b) { D2D1_ROUNDED_RECT rr{
                    D2D1::RectF(sRx+ins, sRy+ins, sRx+scaledKW-ins, sRy+scaledKH-ins*1.5f),
                    (kCorner-ins)*scl, (kCorner-ins)*scl};
                    rt->FillRoundedRectangle(rr, b.Get()); }
            }
            // Keycap border
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(
                    sel ? Tok::GoldHi(0.96f*ease) : Tok::InkLine(0.84f*ease),
                    b.GetAddressOf());
                if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(sRx,sRy,sRx+scaledKW,sRy+scaledKH),kCorner*scl,kCorner*scl};
                         rt->DrawRoundedRectangle(rr, b.Get(), sel ? 1.8f : 0.85f); }
            }
            // Bevel shadow rim
            {
                const float bi = 1.2f;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(
                    sel ? Tok::GoldShadow(0.24f*ease) : Tok::GoldDeep(0.13f*ease),
                    b.GetAddressOf());
                if (b) { D2D1_ROUNDED_RECT rr{
                    D2D1::RectF(sRx+bi,sRy+bi,sRx+scaledKW-bi,sRy+scaledKH-bi),
                    (kCorner-bi)*scl, (kCorner-bi)*scl};
                    rt->DrawRoundedRectangle(rr, b.Get(), 0.55f); }
            }
            // Top specular arc
            if (fac) {
                DrawArcSpecular(rt, fac.Get(),
                    kCx, sRy + scaledKH * 0.28f,
                    scaledKW * 0.28f, scaledKH * 0.20f,
                    (sel ? 0.12f : 0.06f) * ease);
            }

            // Key label
            if (dwrite) {
                const std::wstring disp = KeyDisplay(k);
                if (!disp.empty()) {
                    const float fontSize = (k.isSpecial ? kFSpec_base : kFKey_base) * s * scl;
                    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
                    dwrite->CreateTextFormat(
                        k.isSpecial ? L"Segoe UI Emoji" : L"Segoe UI",
                        nullptr,
                        k.isSpecial ? DWRITE_FONT_WEIGHT_NORMAL : DWRITE_FONT_WEIGHT_SEMI_BOLD,
                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                        fontSize, L"en-us", fmt.GetAddressOf());
                    if (fmt) {
                        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> lb;
                        rt->CreateSolidColorBrush(
                            sel       ? Tok::GoldBright(0.99f*ease)
                            : k.isSpecial ? Tok::ChromeMid(0.68f*ease)
                            :               Tok::ChromeHi(0.82f*ease),
                            lb.GetAddressOf());
                        if (lb) rt->DrawText(disp.c_str(),
                            static_cast<UINT32>(disp.size()), fmt.Get(),
                            D2D1::RectF(sRx, sRy, sRx+scaledKW, sRy+scaledKH), lb.Get());
                    }
                }
            }

            rx += kw + kGap;
        }
    }
}

} // namespace enjoystick::overlay
