#include <enjoystick/overlay/VirtualKeyboard.hpp>
#include "Overlay_Theme.hpp"
#include "Overlay_SpringAnim.hpp"

#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

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
// Layout constants — large, far-field readable keys
// ---------------------------------------------------------------------------
static constexpr float kKeyW_base   = 88.0f;  // wider: was 80
static constexpr float kKeyH_base   = 72.0f;  // taller: was 68
static constexpr float kGap_base    =  7.0f;
static constexpr float kCorner_base = 11.0f;
static constexpr float kPadX_base   = 24.0f;
static constexpr float kPadY_base   = 18.0f;
static constexpr float kTbH_base    = 52.0f;
static constexpr float kHintH_base  = 26.0f;
static constexpr float kFKey_base   = 24.0f;  // was 21
static constexpr float kFSpec_base  = 22.0f;  // was 20
static constexpr float kFText_base  = 17.0f;
static constexpr float kFHint_base  = 13.5f;
static constexpr float kFBadge_base = 11.0f;
static constexpr float kAccentH_base = 3.0f;

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
void VirtualKeyboard::BuildLayout() {
    m_rows.clear();
    m_rows.push_back({
        {L"1",L"!",L"\u00B1"}, {L"2",L"@",L"\u00B2"}, {L"3",L"#",L"\u00B3"},
        {L"4",L"$",L"\u00A3"}, {L"5",L"%",L"\u20AC"}, {L"6",L"^",L"\u00B6"},
        {L"7",L"&",L"\u2022"}, {L"8",L"*",L"\u00D7"}, {L"9",L"(",L"\u2018"},
        {L"0",L")",L"\u2019"},
        {L"\u232B",L"\u232B",L"\u232B", 1.5f, true},
    });
    m_rows.push_back({
        {L"q",L"Q",L"-"}, {L"w",L"W",L"_"}, {L"e",L"E",L"="},
        {L"r",L"R",L"+"}, {L"t",L"T",L"["}, {L"y",L"Y",L"]"},
        {L"u",L"U",L"{"}, {L"i",L"I",L"}"}, {L"o",L"O",L"\\\\"},
        {L"p",L"P",L"|"},
    });
    m_rows.push_back({
        {L"a",L"A",L"~"}, {L"s",L"S",L"`"}, {L"d",L"D",L"<"},
        {L"f",L"F",L">"}, {L"g",L"G",L","}, {L"h",L"H",L"."},
        {L"j",L"J",L"\u2026"}, {L"k",L"K",L"\u2014"}, {L"l",L"L",L":"},
        {L"\u23CE",L"\u23CE",L"\u23CE", 1.5f, true},
    });
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

Vec2 VirtualKeyboard::KeyCentrePixel(int32_t row, int32_t col,
                                      float dpiScale, float screenW, float) const noexcept
{
    const float s     = dpiScale;
    const float kKeyW = kKeyW_base * s;
    const float kKeyH = kKeyH_base * s;
    const float kGap  = kGap_base  * s;
    const float kPadX = kPadX_base * s;
    const float kPadY = kPadY_base * s;
    const float kTbH  = kTbH_base  * s;

    float maxRowW = 0.0f;
    for (const auto& r : m_rows) {
        float rw = -kGap;
        for (const auto& k : r) rw += k.widthMul * kKeyW + kGap;
        if (rw > maxRowW) maxRowW = rw;
    }
    const float kPanelW = maxRowW + kPadX * 2.0f;
    const float panelX  = (screenW - kPanelW) * 0.5f;
    const float keysTop = kPadY + kTbH + 8.0f * s + kAccentH_base * s;

    if (row < 0 || row >= static_cast<int32_t>(m_rows.size())) return {-9999.0f,-9999.0f};
    const auto& rrow = m_rows[static_cast<size_t>(row)];
    if (col < 0 || col >= static_cast<int32_t>(rrow.size())) return {-9999.0f,-9999.0f};

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
    m_row           = 3; m_col = 9;
    m_layer         = Layer::Alpha;
    m_shift         = false; m_caps = false;
    m_glowPhase     = 0.0f;
    m_state         = State::Opening;
    m_stickCooldown = 0.0f; m_stickActive = false;
    m_dpadHeld      = false; m_dpadTimer = 0.0f;
    m_dpadDirRow    = 0; m_dpadDirCol = 0;
    m_prevSouth = m_prevEast = m_prevWest = m_prevNorth =
    m_prevLB    = m_prevRB  = m_prevLS   = false;

    m_panelSpring.stiffness = 320.0f;
    m_panelSpring.damping   = 24.0f;
    m_panelSpring.Snap(0.0f);
    m_panelSpring.SetTarget(1.0f);

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
// Direct per-char SendInput helper (types into focused window immediately)
// ---------------------------------------------------------------------------
static void SendCharDirect(wchar_t ch) noexcept {
    INPUT inp[2]{};
    inp[0].type       = INPUT_KEYBOARD;
    inp[0].ki.wVk     = 0;
    inp[0].ki.wScan   = ch;
    inp[0].ki.dwFlags = KEYEVENTF_UNICODE;
    inp[1]            = inp[0];
    inp[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    SendInput(2, inp, sizeof(INPUT));
}
static void SendBackspaceDirect() noexcept {
    INPUT inp[2]{};
    inp[0].type    = INPUT_KEYBOARD;
    inp[0].ki.wVk  = VK_BACK;
    inp[1]         = inp[0];
    inp[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inp, sizeof(INPUT));
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
void VirtualKeyboard::Update(const ControllerState& state, float dt) {
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
    const bool dUp   = HasButton(state.buttons, Button::DPadUp);
    const bool dDown = HasButton(state.buttons, Button::DPadDown);
    const bool dLeft = HasButton(state.buttons, Button::DPadLeft);
    const bool dRight= HasButton(state.buttons, Button::DPadRight);

    if (east  && !m_prevEast)  { Close(); goto done; }
    if (north && !m_prevNorth) { if (m_onSubmit) m_onSubmit(m_text); Close(); goto done; }
    if (west  && !m_prevWest)  {
        // Backspace: remove from internal buffer AND send to focused window
        if (!m_text.empty()) m_text.pop_back();
        SendBackspaceDirect();
        goto done;
    }
    if (ls    && !m_prevLS)    { m_caps = !m_caps; m_shift = false; goto done; }
    if (lb    && !m_prevLB)    { m_layer = (m_layer == Layer::Alpha) ? Layer::Sym : Layer::Alpha; goto done; }
    if (rb    && !m_prevRB)    { m_layer = Layer::Alpha; goto done; }
    if (south && !m_prevSouth) {
        if (const Key* k = CurrentKey()) {
            TypeKey(*k);
            // Pop scale animation
            m_cursorScaleSpring.value    = 1.12f;
            m_cursorScaleSpring.velocity = 0.0f;
            m_cursorScaleSpring.SetTarget(1.0f);
        }
        goto done;
    }

    // -------------------------------------------------------------------------
    // DPad navigation
    // -------------------------------------------------------------------------
    {
        const int32_t dr = dDown ? 1 : (dUp   ? -1 : 0);
        const int32_t dc = dRight? 1 : (dLeft ? -1 : 0);
        const bool dAny  = (dr != 0 || dc != 0);
        if (dAny) {
            if (!m_dpadHeld) {
                m_dpadHeld   = true;
                m_dpadTimer  = kDPadFirst;
                m_dpadDirRow = dr;
                m_dpadDirCol = dc;
                NavigateTo(m_row + dr, m_col + dc);
            } else {
                m_dpadTimer -= dt;
                if (m_dpadTimer <= 0.0f) {
                    m_dpadTimer = kDPadNext;
                    NavigateTo(m_row + m_dpadDirRow, m_col + m_dpadDirCol);
                }
            }
        } else {
            m_dpadHeld = false;
            m_dpadTimer = 0.0f;
        }
    }

    // -------------------------------------------------------------------------
    // Left-stick navigation with strong magnetic snap
    // -------------------------------------------------------------------------
    {
        const float lx = state.leftStick.x;
        const float ly = state.leftStick.y;
        const float dz = kSnapDeadzone;
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
        if (k.label == L"\u232B") {
            if (!m_text.empty()) m_text.pop_back();
            SendBackspaceDirect();
            return;
        }
        if (k.label == L"\u23CE") {
            m_text += L'\n';
            SendCharDirect(L'\n');
            if (m_onChar) m_onChar(L'\n');
            return;
        }
        if (k.label == L"\u21E7") { m_shift = !m_shift; return; }
        if (k.label == L"\u2423") {
            m_text += L' ';
            SendCharDirect(L' ');
            if (m_onChar) m_onChar(L' ');
            return;
        }
        return;
    }
    const std::wstring d = KeyDisplay(k);
    if (d.size() == 1) {
        m_text += d[0];
        SendCharDirect(d[0]);
        if (m_onChar) m_onChar(d[0]);
    }
    if (m_shift && !m_caps) m_shift = false;
}

// ---------------------------------------------------------------------------
// Draw helpers
// ---------------------------------------------------------------------------
namespace {

static void DrawArcSpecular(
    ID2D1RenderTarget* rt, ID2D1Factory* fac,
    float cx, float cy, float rx, float ry, float alpha) noexcept
{
    if (!rt || !fac || rx < 2.0f || ry < 2.0f || alpha < 0.004f) return;
    const float aFrom = -kPi * 0.84f;
    const float aTo   = -kPi * 0.16f;
    const float arx   = rx - 1.5f, ary = ry - 1.5f;
    if (arx <= 0.0f || ary <= 0.0f) return;

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> g;
    fac->CreatePathGeometry(g.GetAddressOf());
    if (!g) return;
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sk;
    g->Open(sk.GetAddressOf());
    if (!sk) return;
    sk->BeginFigure(
        D2D1::Point2F(cx + arx * std::cos(aFrom), cy + ary * std::sin(aFrom)),
        D2D1_FIGURE_BEGIN_HOLLOW);
    D2D1_ARC_SEGMENT arc{};
    arc.point          = D2D1::Point2F(cx + arx * std::cos(aTo), cy + ary * std::sin(aTo));
    arc.size           = D2D1::SizeF(arx, ary);
    arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
    arc.arcSize        = D2D1_ARC_SIZE_SMALL;
    sk->AddArc(arc);
    sk->EndFigure(D2D1_FIGURE_END_OPEN);
    sk->Close();
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
    rt->CreateSolidColorBrush(Tok::White(alpha), b.GetAddressOf());
    if (b) rt->DrawGeometry(g.Get(), b.Get(), 0.9f);
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

static float MeasureTextWidth(
    IDWriteFactory* dwrite,
    const wchar_t*  text,
    UINT32          len,
    const wchar_t*  fontFamily,
    DWRITE_FONT_WEIGHT weight,
    float           fontSize) noexcept
{
    if (!dwrite || len == 0) return 0.0f;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    dwrite->CreateTextFormat(fontFamily, nullptr,
        weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        fontSize, L"en-us", fmt.GetAddressOf());
    if (!fmt) return 0.0f;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> lay;
    dwrite->CreateTextLayout(text, len,
        fmt.Get(), 4096.0f, fontSize + 8.0f, lay.GetAddressOf());
    if (!lay) return 0.0f;
    DWRITE_TEXT_METRICS m{};
    lay->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

} // anon

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
    if (m_state == State::Hidden) return;
    const float panelVal = std::max(0.0f, std::min(1.0f, m_panelSpring.value));
    if (panelVal <= 0.001f) return;
    if (!renderTargetPtr || !dwriteFactoryPtr) return;

    auto* rt     = static_cast<ID2D1RenderTarget*>(renderTargetPtr);
    auto* dwrite = static_cast<IDWriteFactory*>(dwriteFactoryPtr);

    Microsoft::WRL::ComPtr<ID2D1Factory> fac;
    rt->GetFactory(fac.GetAddressOf());

    const float s    = dpiScale;
    const float ease = 1.0f - std::pow(1.0f - panelVal, 3.0f);

    // ---- Layout ----------------------------------------------------------------
    const float kKeyW   = kKeyW_base   * s;
    const float kKeyH   = kKeyH_base   * s;
    const float kGap    = kGap_base    * s;
    const float kCorner = kCorner_base * s;
    const float kPadX   = kPadX_base   * s;
    const float kPadY   = kPadY_base   * s;
    const float kTbH    = kTbH_base    * s;
    const float kHintH  = kHintH_base  * s;
    const float kAccH   = kAccentH_base * s;

    float maxRowW = 0.0f;
    for (const auto& row : m_rows) {
        float rw = -kGap;
        for (const auto& k : row) rw += k.widthMul * kKeyW + kGap;
        if (rw > maxRowW) maxRowW = rw;
    }
    const float kPanelW = maxRowW + kPadX * 2.0f;
    const float kPanelH = kAccH + kPadY + kTbH + 8.0f*s
                        + static_cast<float>(m_rows.size()) * (kKeyH + kGap) - kGap
                        + kHintH + kPadY;

    const float targetY = screenH - kPanelH - 28.0f * s;
    const float panelY  = targetY + (1.0f - panelVal) * (screenH - targetY + 28.0f * s);
    const float panelX  = (screenW - kPanelW) * 0.5f;
    const float panelR  = 14.0f * s;

    // ---- Panel fill
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceBase(0.96f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(panelX, panelY, panelX+kPanelW, panelY+kPanelH), panelR, panelR};
                 rt->FillRoundedRectangle(rr, b.Get()); }
    }
    // ---- Gold accent bar at top
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldMid(0.88f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(panelX, panelY, panelX+kPanelW, panelY + kAccH + panelR), panelR, panelR};
                 rt->FillRoundedRectangle(rr, b.Get()); }
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceBase(0.96f * ease), b.GetAddressOf());
        if (b) rt->FillRectangle(
            D2D1::RectF(panelX, panelY + kAccH, panelX + kPanelW, panelY + kAccH + panelR), b.Get());
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceBase(0.96f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(panelX, panelY + kAccH, panelX+kPanelW, panelY+kPanelH), panelR, panelR};
                 rt->FillRoundedRectangle(rr, b.Get()); }
    }
    // ---- Panel border
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldShadow(0.35f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(panelX+0.5f, panelY+0.5f, panelX+kPanelW-0.5f, panelY+kPanelH-0.5f), panelR, panelR};
                 rt->DrawRoundedRectangle(rr, b.Get(), 0.9f); }
    }

    // ---- Text bar
    const float tbX0 = panelX + kPadX;
    const float tbX1 = panelX + kPanelW - kPadX;
    const float tbY  = panelY + kAccH + kPadY;
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceSunken(0.96f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(tbX0, tbY, tbX1, tbY+kTbH), 7.0f*s, 7.0f*s};
                 rt->FillRoundedRectangle(rr, b.Get()); }
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldDeep(0.60f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(tbX0, tbY, tbX1, tbY+kTbH), 7.0f*s, 7.0f*s};
                 rt->DrawRoundedRectangle(rr, b.Get(), 1.0f); }
    }
    if (dwrite) {
        const std::wstring display = m_text.size() > 48
            ? L"\u2026" + m_text.substr(m_text.size() - 47) : m_text;
        const float textAreaW = tbX1 - tbX0 - 24.0f * s;
        const float textFontSz = kFText_base * s;

        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
        dwrite->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, textFontSz, L"en-us", fmt.GetAddressOf());
        if (fmt) {
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(Tok::ChromeHi(0.94f * ease), b.GetAddressOf());
            if (b) rt->DrawText(display.c_str(), static_cast<UINT32>(display.size()),
                fmt.Get(), D2D1::RectF(tbX0 + 12.0f*s, tbY, tbX1 - 12.0f*s, tbY+kTbH), b.Get());
        }

        // Blinking cursor after last character
        {
            const float blink   = 0.5f + 0.5f * std::sin(m_glowPhase * 2.0f);
            const float textInnerX = tbX0 + 12.0f * s;

            float textPixelW = MeasureTextWidth(
                dwrite,
                display.c_str(),
                static_cast<UINT32>(display.size()),
                L"Segoe UI",
                DWRITE_FONT_WEIGHT_NORMAL,
                textFontSz);
            textPixelW = std::min(textPixelW, textAreaW);

            const float cursorX = textInnerX + textPixelW;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(Tok::GoldHi(blink * 0.90f * ease), b.GetAddressOf());
            if (b) rt->DrawLine(
                D2D1::Point2F(cursorX, tbY + 9.0f * s),
                D2D1::Point2F(cursorX, tbY + kTbH - 9.0f * s),
                b.Get(), 1.8f * s);
        }
    }

    // ---- Hint bar
    {
        const float hy  = panelY + kPanelH - kHintH - kPadY * 0.55f;
        const float hcy = hy + kHintH * 0.5f;
        const float fnt = kFHint_base * s;
        float hx = panelX + kPadX;
        const float chipGap = 10.0f * s;

        DrawHintChip(rt, dwrite, hx, hcy, s, ease,
            L"\u25CF  Type",
            Tok::GoldDeep(0.70f * ease), Tok::GoldHi(0.92f * ease), fnt);
        hx += 88.0f * s + chipGap;
        DrawHintChip(rt, dwrite, hx, hcy, s, ease,
            L"\u25A0  \u232B",
            Tok::SurfaceRaised(0.90f * ease), Tok::ChromeMid(0.80f * ease), fnt);
        hx += 76.0f * s + chipGap;
        // Submit chip: more prominent gold fill so user sees it clearly
        DrawHintChip(rt, dwrite, hx, hcy, s, ease,
            L"\u25B2  Submit",
            Tok::GoldMid(0.75f * ease), Tok::GoldBright(0.97f * ease), fnt);
        hx += 96.0f * s + chipGap;
        DrawHintChip(rt, dwrite, hx, hcy, s, ease,
            L"\u25C6  Cancel",
            Tok::SurfaceRaised(0.90f * ease), Tok::ChromeMid(0.80f * ease), fnt);
        hx += 90.0f * s + chipGap;
        if (m_layer == Layer::Sym || m_caps) {
            const wchar_t* badge = (m_layer == Layer::Sym) ? L"SYM" : L"CAPS";
            DrawHintChip(rt, dwrite, hx, hcy, s, ease, badge,
                (m_layer == Layer::Sym) ? Tok::GoldDeep(0.85f * ease) : Tok::AmberWarm(0.65f * ease),
                Tok::GoldAccent(0.97f * ease), fnt);
        }
    }

    // ---- Keys
    const float keysTop = panelY + kAccH + kPadY + kTbH + 8.0f * s;

    const Vec2 selCentre = KeyCentrePixel(m_row, m_col, s, screenW, screenH);

    m_cursorSpringX.target = selCentre.x;
    m_cursorSpringY.target = selCentre.y;
    if (m_cursorSpringX.value == 0.0f && m_cursorSpringY.value == 0.0f) {
        m_cursorSpringX.Snap(selCentre.x);
        m_cursorSpringY.Snap(selCentre.y);
    }

    const float scl = m_cursorScaleSpring.value;

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
            const float sc2 = sel ? scl : 1.0f;
            const float skw = kw    * sc2;
            const float skh = kKeyH * sc2;
            const float sRx = kCx - skw * 0.5f;
            const float sRy = kCy - skh * 0.5f;
            const float cr  = kCorner * sc2;

            // Key body
            { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
              rt->CreateSolidColorBrush(
                  sel ? Tok::SurfaceRaised(0.97f * ease) : Tok::SurfaceSunken(0.90f * ease),
                  b.GetAddressOf());
              if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(sRx,sRy,sRx+skw,sRy+skh), cr, cr};
                       rt->FillRoundedRectangle(rr, b.Get()); } }

            // Inner bevel
            { const float ins = 2.4f * s;
              Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
              rt->CreateSolidColorBrush(
                  sel ? Tok::SurfaceBase(0.92f * ease) : Tok::SurfaceRaised(0.72f * ease),
                  b.GetAddressOf());
              if (b) { D2D1_ROUNDED_RECT rr{
                  D2D1::RectF(sRx+ins, sRy+ins, sRx+skw-ins, sRy+skh-ins*1.4f),
                  std::max(cr-ins, 0.0f), std::max(cr-ins, 0.0f) };
                  rt->FillRoundedRectangle(rr, b.Get()); } }

            // ---- Focus ring: crisp 2px GoldHi border on selected key ONLY.
            // No circular glow blobs; a clean rectangle ring matches key shape
            // and reads clearly at distance (Steam keyboard style).
            if (sel) {
                // Outer ring — bright gold
                { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                  rt->CreateSolidColorBrush(Tok::GoldHi(0.90f * ease), b.GetAddressOf());
                  if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(sRx,sRy,sRx+skw,sRy+skh), cr, cr};
                           rt->DrawRoundedRectangle(rr, b.Get(), 2.2f * s); } }
                // Inner glow stripe — subtle warmth just inside the border
                { const float g = 3.0f * s;
                  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                  rt->CreateSolidColorBrush(Tok::GoldWarm(0.18f * ease), b.GetAddressOf());
                  if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(sRx+g,sRy+g,sRx+skw-g,sRy+skh-g),
                           std::max(cr-g,0.0f), std::max(cr-g,0.0f)};
                           rt->DrawRoundedRectangle(rr, b.Get(), 1.0f * s); } }
            } else {
                // Unselected: very dim thin border for key shape definition
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(Tok::InkLine(0.72f * ease), b.GetAddressOf());
                if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(sRx,sRy,sRx+skw,sRy+skh), cr, cr};
                         rt->DrawRoundedRectangle(rr, b.Get(), 0.7f); }
            }

            // Inner shadow ring (depth)
            { const float bi = 1.0f;
              Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
              rt->CreateSolidColorBrush(
                  sel ? Tok::GoldShadow(0.22f * ease) : Tok::GoldDeep(0.08f * ease),
                  b.GetAddressOf());
              if (b) { D2D1_ROUNDED_RECT rr{
                  D2D1::RectF(sRx+bi, sRy+bi, sRx+skw-bi, sRy+skh-bi),
                  std::max(cr-bi, 0.0f), std::max(cr-bi, 0.0f) };
                  rt->DrawRoundedRectangle(rr, b.Get(), 0.5f); } }

            if (fac) {
                DrawArcSpecular(rt, fac.Get(),
                    kCx, sRy + skh * 0.28f, skw * 0.26f, skh * 0.18f,
                    (sel ? 0.10f : 0.04f) * ease);
            }

            if (dwrite) {
                const std::wstring disp = KeyDisplay(k);
                if (!disp.empty()) {
                    const float fs = (k.isSpecial ? kFSpec_base : kFKey_base) * s * sc2;
                    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
                    dwrite->CreateTextFormat(
                        k.isSpecial ? L"Segoe UI Symbol" : L"Segoe UI", nullptr,
                        k.isSpecial ? DWRITE_FONT_WEIGHT_NORMAL : DWRITE_FONT_WEIGHT_SEMI_BOLD,
                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                        fs, L"en-us", fmt.GetAddressOf());
                    if (fmt) {
                        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> lb;
                        rt->CreateSolidColorBrush(
                            sel         ? Tok::GoldBright(0.99f * ease)
                            : k.isSpecial ? Tok::ChromeMid(0.70f * ease)
                            :               Tok::ChromeHi(0.82f * ease),
                            lb.GetAddressOf());
                        if (lb) rt->DrawText(disp.c_str(),
                            static_cast<UINT32>(disp.size()), fmt.Get(),
                            D2D1::RectF(sRx, sRy, sRx+skw, sRy+skh), lb.Get());
                    }
                }
            }

            rx += kw + kGap;
        }
    }
}

} // namespace enjoystick::overlay
