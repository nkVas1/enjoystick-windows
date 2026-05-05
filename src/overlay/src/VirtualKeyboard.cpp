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
#include <limits>

#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif
static constexpr float kPi = static_cast<float>(M_PI);

namespace enjoystick::overlay {

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------
static constexpr float kKeyW_base    = 88.0f;
static constexpr float kKeyH_base    = 72.0f;
static constexpr float kGap_base     =  7.0f;
static constexpr float kCorner_base  = 11.0f;
static constexpr float kPadX_base    = 24.0f;
static constexpr float kPadY_base    = 18.0f;
static constexpr float kHintH_base   = 26.0f;
static constexpr float kFKey_base    = 24.0f;
static constexpr float kFSpec_base   = 22.0f;
static constexpr float kFHint_base   = 13.5f;
static constexpr float kAccentH_base =  3.0f;
// Text bar removed — characters are sent directly via SendInput to the
// focused window, so an internal preview is unnecessary.

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
void VirtualKeyboard::BuildLayout() {
    m_rows.clear();
    m_rows.push_back({
        {L"1",L"!",L"\u00B1",L"1",L"!"},
        {L"2",L"@",L"\u00B2",L"2",L"@"},
        {L"3",L"#",L"\u00B3",L"3",L"#"},
        {L"4",L"$",L"\u00A3",L"4",L";"},
        {L"5",L"%",L"\u20AC",L"5",L"%"},
        {L"6",L"^",L"\u00B6",L"6",L":"},
        {L"7",L"&",L"\u2022",L"7",L"?"},
        {L"8",L"*",L"\u00D7",L"8",L"*"},
        {L"9",L"(",L"\u2018",L"9",L"("},
        {L"0",L")",L"\u2019",L"0",L")"},
        {L"\u232B",L"\u232B",L"\u232B",L"\u232B",L"\u232B",1.5f,true},
    });
    m_rows.push_back({
        {L"q",L"Q",L"-",L"\u0439",L"\u0419"},
        {L"w",L"W",L"_",L"\u0446",L"\u0426"},
        {L"e",L"E",L"=",L"\u0443",L"\u0423"},
        {L"r",L"R",L"+",L"\u043A",L"\u041A"},
        {L"t",L"T",L"[",L"\u0435",L"\u0415"},
        {L"y",L"Y",L"]",L"\u043D",L"\u041D"},
        {L"u",L"U",L"{",L"\u0433",L"\u0413"},
        {L"i",L"I",L"}",L"\u0448",L"\u0428"},
        {L"o",L"O",L"\\\\",L"\u0449",L"\u0429"},
        {L"p",L"P",L"|",L"\u0437",L"\u0417"},
    });
    m_rows.push_back({
        {L"a",L"A",L"~",L"\u0444",L"\u0424"},
        {L"s",L"S",L"`",L"\u044B",L"\u042B"},
        {L"d",L"D",L"<",L"\u0432",L"\u0412"},
        {L"f",L"F",L">",L"\u0430",L"\u0410"},
        {L"g",L"G",L",",L"\u043F",L"\u041F"},
        {L"h",L"H",L".",L"\u0440",L"\u0420"},
        {L"j",L"J",L"\u2026",L"\u043E",L"\u041E"},
        {L"k",L"K",L"\u2014",L"\u043B",L"\u041B"},
        {L"l",L"L",L":",L"\u0434",L"\u0414"},
        {L"\u23CE",L"\u23CE",L"\u23CE",L"\u23CE",L"\u23CE",1.5f,true},
    });
    m_rows.push_back({
        {L"\u21E7",L"\u21E7",L"\u21E7",L"\u21E7",L"\u21E7",1.5f,true},
        {L"z",L"Z",L"/",L"\u0436",L"\u0416"},
        {L"x",L"X",L"?",L"\u044D",L"\u042D"},
        {L"c",L"C",L"\'",L"\u0441",L"\u0421"},
        {L"v",L"V",L"\"",L"\u043C",L"\u041C"},
        {L"b",L"B",L" ",L"\u0438",L"\u0418"},
        {L"n",L"N",L"\u2122",L"\u0442",L"\u0422"},
        {L"m",L"M",L"\u00AE",L"\u044C",L"\u042C"},
        {L".",L".",L",",L"\u0431",L"\u0411"},
        {L"\u2423",L"\u2423",L"\u2423",L"\u044E",L"\u042E"},
        {L"\u2423",L"\u2423",L"\u2423",L"\u2423",L"\u2423",2.5f,true},
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
    if (m_layer == Layer::Cyr)  return (m_shift || m_caps) ? k.cyrShift : k.cyrLabel;
    if (m_shift || m_caps)      return k.shiftLabel;
    return k.label;
}
void VirtualKeyboard::NavigateTo(int32_t row, int32_t col) {
    const int32_t nr = static_cast<int32_t>(m_rows.size());
    row = (row % nr + nr) % nr;
    const int32_t n = RowKeyCount(row);
    col = (n > 0) ? (col % n + n) % n : 0;
    m_row = row; m_col = col;
    // Bounce pop
    m_cursorScaleSpring.value    = 1.09f;
    m_cursorScaleSpring.velocity = 0.0f;
    m_cursorScaleSpring.SetTarget(1.0f);
    // Kick trail alpha so the blob is fully opaque right after a hop
    m_trailAlpha = 1.0f;
    // Haptic: soft nav tick
    if (m_onHaptic) m_onHaptic(0.0f, 0.18f, 18);
}

Vec2 VirtualKeyboard::KeyCentrePixel(int32_t row, int32_t col,
                                      float dpiScale, float screenW, float screenH) const noexcept
{
    const float s     = dpiScale;
    const float kKeyW = kKeyW_base * s;
    const float kKeyH = kKeyH_base * s;
    const float kGap  = kGap_base  * s;
    const float kPadX = kPadX_base * s;

    float maxRowW = 0.0f;
    for (const auto& r : m_rows) {
        float rw = -kGap;
        for (const auto& k : r) rw += k.widthMul * kKeyW + kGap;
        if (rw > maxRowW) maxRowW = rw;
    }
    const float kPanelW = maxRowW + kPadX * 2.0f;
    const float panelX  = (screenW - kPanelW) * 0.5f;
    const float kPadY   = kPadY_base * s;
    // No text bar — keys start right after the accent strip + top padding
    const float keysTop = kPadY + kAccentH_base * s;

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
    (void)screenH;
    return { rx + kw * 0.5f, ry + kKeyH * 0.5f };
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void VirtualKeyboard::Open(const std::wstring& seed) {
    if (m_state == State::Visible || m_state == State::Opening) return;
    BuildLayout();
    m_text          = seed;
    m_row           = 3; m_col = 10; // wide space bar
    m_layer         = Layer::Alpha;
    m_shift         = false; m_caps = false;
    m_glowPhase     = 0.0f;
    m_state         = State::Opening;
    m_stickCooldown = 0.0f; m_stickActive = false;
    m_stickHoldTime = 0.0f;
    m_dpadHeld      = false; m_dpadTimer = 0.0f;
    m_dpadDirRow    = 0; m_dpadDirCol = 0;
    m_lbDebounce    = 0.0f;

    m_southHeld = false; m_southHoldTime = 0.0f; m_southRepeatTimer = 0.0f;
    m_westHeld  = false; m_westHoldTime  = 0.0f; m_westRepeatTimer  = 0.0f;

    m_prevSouth = m_prevEast = m_prevWest = m_prevNorth =
    m_prevLB    = m_prevRB  = m_prevLS   = false;

    m_rStickActive = false;
    m_rStickX = 0.0f; m_rStickY = 0.0f;

    m_trailAlpha = 0.0f;

    m_panelSpring.stiffness = 320.0f;
    m_panelSpring.damping   = 24.0f;
    m_panelSpring.Snap(0.0f);
    m_panelSpring.SetTarget(1.0f);

    m_cursorSpringX.stiffness = 520.0f; m_cursorSpringX.damping = 28.0f;
    m_cursorSpringY.stiffness = 520.0f; m_cursorSpringY.damping = 28.0f;
    m_cursorScaleSpring.stiffness = 600.0f; m_cursorScaleSpring.damping = 28.0f;
    m_cursorScaleSpring.Snap(1.0f);

    // Viscous trail spring — low stiffness so it visibly lags the cursor
    m_trailSpringX.stiffness = 38.0f; m_trailSpringX.damping = 9.0f;
    m_trailSpringY.stiffness = 38.0f; m_trailSpringY.damping = 9.0f;
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
// Direct per-char SendInput helper
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
    const float dtMs = dt * 1000.0f;

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
    m_trailSpringX.Step(dt);
    m_trailSpringY.Step(dt);
    m_cursorScaleSpring.Step(dt);

    // Decay trail alpha
    if (m_trailAlpha > 0.0f) {
        m_trailAlpha -= dt * kTrailDecayRate;
        if (m_trailAlpha < 0.0f) m_trailAlpha = 0.0f;
    }

    // LB debounce tick
    if (m_lbDebounce > 0.0f) {
        m_lbDebounce -= dtMs;
        if (m_lbDebounce < 0.0f) m_lbDebounce = 0.0f;
    }

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

    if (ls && !m_prevLS) { m_caps = !m_caps; m_shift = false; goto done; }

    // LB layer-cycle — debounced
    if (lb && !m_prevLB && m_lbDebounce <= 0.0f) {
        if      (m_layer == Layer::Alpha) m_layer = Layer::Cyr;
        else if (m_layer == Layer::Cyr)   m_layer = Layer::Sym;
        else                              m_layer = Layer::Alpha;
        m_lbDebounce = kLbDebounceMs;
        goto done;
    }
    if (rb && !m_prevRB) { m_layer = Layer::Alpha; goto done; }

    // -------------------------------------------------------------------------
    // West — backspace  (hold to repeat with acceleration)
    // -------------------------------------------------------------------------
    if (west) {
        if (!m_westHeld) {
            // First press
            m_westHeld        = true;
            m_westHoldTime    = 0.0f;
            m_westRepeatTimer = kBsFirstMs;
            if (!m_text.empty()) m_text.pop_back();
            SendBackspaceDirect();
            if (m_onHaptic) m_onHaptic(0.0f, 0.30f, 22);
            m_cursorScaleSpring.value    = 1.10f;
            m_cursorScaleSpring.velocity = 0.0f;
            m_cursorScaleSpring.SetTarget(1.0f);
        } else {
            m_westHoldTime    += dt;
            m_westRepeatTimer -= dtMs;
            if (m_westRepeatTimer <= 0.0f) {
                // Blend interval
                const float t    = std::max(0.0f,
                    (m_westHoldTime - kBsAccelStart) / kBsAccelRange);
                const float blend= std::min(1.0f, t * t);
                m_westRepeatTimer = kBsRepeatSlowMs +
                    (kBsRepeatFastMs - kBsRepeatSlowMs) * blend;
                if (!m_text.empty()) m_text.pop_back();
                SendBackspaceDirect();
                if (m_onHaptic) m_onHaptic(0.0f, 0.18f, 14);
            }
        }
        goto done;
    } else {
        m_westHeld     = false;
        m_westHoldTime = 0.0f;
        m_westRepeatTimer = 0.0f;
    }

    // -------------------------------------------------------------------------
    // South — type key  (hold to repeat with acceleration)
    // -------------------------------------------------------------------------
    if (south) {
        if (!m_southHeld) {
            // First press
            m_southHeld        = true;
            m_southHoldTime    = 0.0f;
            m_southRepeatTimer = kTypeFirstMs;
            if (const Key* k = CurrentKey()) {
                TypeKey(*k);
                m_cursorScaleSpring.value    = 1.12f;
                m_cursorScaleSpring.velocity = 0.0f;
                m_cursorScaleSpring.SetTarget(1.0f);
                if (m_onHaptic) m_onHaptic(0.0f, 0.35f, 25);
            }
        } else {
            m_southHoldTime    += dt;
            m_southRepeatTimer -= dtMs;
            if (m_southRepeatTimer <= 0.0f) {
                const float t    = std::max(0.0f,
                    (m_southHoldTime - kTypeAccelStart) / kTypeAccelRange);
                const float blend= std::min(1.0f, t * t);
                m_southRepeatTimer = kTypeRepeatSlowMs +
                    (kTypeRepeatFastMs - kTypeRepeatSlowMs) * blend;
                if (const Key* k = CurrentKey()) {
                    TypeKey(*k);
                    if (m_onHaptic) m_onHaptic(0.0f, 0.18f, 14);
                }
            }
        }
        goto done;
    } else {
        m_southHeld     = false;
        m_southHoldTime = 0.0f;
        m_southRepeatTimer = 0.0f;
    }

    // -------------------------------------------------------------------------
    // Right-stick proximity hover
    // When the right stick is pushed the key nearest to the projected position
    // is selected.  Left-stick / DPad navigation is suppressed while active.
    // -------------------------------------------------------------------------
    {
        const float rx = state.rightStick.x;
        const float ry = state.rightStick.y;
        const float rm = std::sqrt(rx * rx + ry * ry);

        if (rm > kRStickDz) {
            m_rStickActive = true;
            // Project onto screen.  Use cached panel centre if available.
            const float sw    = m_cachedScreenW  > 0.0f ? m_cachedScreenW  : 1920.0f;
            const float sh    = m_cachedScreenH  > 0.0f ? m_cachedScreenH  : 1080.0f;
            const float s_dpi = m_cachedDpiScale > 0.0f ? m_cachedDpiScale : 1.0f;
            const float pcx   = sw * 0.5f;
            const float pcy   = m_cachedPanelCY  > 0.0f ? m_cachedPanelCY  : sh * 0.65f;
            const float range = kRStickRange * s_dpi;
            // Smooth the projected position
            const float targetX = pcx + rx * range;
            const float targetY = pcy + ry * range;
            m_rStickX += (targetX - m_rStickX) * std::min(1.0f, dt * 18.0f);
            m_rStickY += (targetY - m_rStickY) * std::min(1.0f, dt * 18.0f);

            // Find closest key centre
            float bestDist = std::numeric_limits<float>::max();
            int32_t bestRow = m_row, bestCol = m_col;
            for (int32_t ri = 0; ri < static_cast<int32_t>(m_rows.size()); ++ri) {
                const auto& row = m_rows[static_cast<size_t>(ri)];
                for (int32_t ci = 0; ci < static_cast<int32_t>(row.size()); ++ci) {
                    const Vec2 c = KeyCentrePixel(ri, ci, s_dpi, sw, sh);
                    const float d = (c.x - m_rStickX) * (c.x - m_rStickX)
                                  + (c.y - m_rStickY) * (c.y - m_rStickY);
                    if (d < bestDist) { bestDist = d; bestRow = ri; bestCol = ci; }
                }
            }
            if (bestRow != m_row || bestCol != m_col)
                NavigateTo(bestRow, bestCol);
        } else {
            if (m_rStickActive) {
                m_rStickActive = false;
                m_rStickX = 0.0f; m_rStickY = 0.0f;
            }
        }
    }

    if (m_rStickActive) goto done; // suppress DPad / left-stick while hovering

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
            m_dpadHeld  = false;
            m_dpadTimer = 0.0f;
        }
    }

    // -------------------------------------------------------------------------
    // Left-stick navigation  (slow first → springy → fast on long hold)
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
                m_stickHoldTime = 0.0f;
                m_stickCooldown = kStickRepeatFirst;
                if (hx) NavigateTo(m_row, m_col + (lx > 0 ? 1 : -1));
                else    NavigateTo(m_row + (ly > 0 ? 1 : -1), m_col);
            } else {
                m_stickHoldTime += dt;
                m_stickCooldown -= dt;
                if (m_stickCooldown <= 0.0f) {
                    const float accelT = std::max(0.0f,
                        (m_stickHoldTime - kStickRepeatAccelStart) / kStickRepeatAccelRange);
                    const float blend  = std::min(1.0f, accelT * accelT);
                    m_stickCooldown = kStickRepeatNext +
                        (kStickRepeatFast - kStickRepeatNext) * blend;
                    if (hx) NavigateTo(m_row, m_col + (lx > 0 ? 1 : -1));
                    else    NavigateTo(m_row + (ly > 0 ? 1 : -1), m_col);
                }
            }
        } else {
            m_stickActive   = false;
            m_stickCooldown = 0.0f;
            m_stickHoldTime = 0.0f;
        }
    }

done:
    m_prevSouth = south; m_prevEast  = east;
    m_prevWest  = west;  m_prevNorth = north;
    m_prevLB    = lb;    m_prevRB    = rb;
    m_prevLS    = ls;
}

bool VirtualKeyboard::TypeKey(const Key& k) {
    if (k.isSpecial) {
        if (k.label == L"\u232B") {
            if (!m_text.empty()) m_text.pop_back();
            SendBackspaceDirect();
            return true;
        }
        if (k.label == L"\u23CE") {
            m_text += L'\n';
            SendCharDirect(L'\n');
            if (m_onChar) m_onChar(L'\n');
            return false;
        }
        if (k.label == L"\u21E7") { m_shift = !m_shift; return false; }
        if (k.label == L"\u2423") {
            m_text += L' ';
            SendCharDirect(L' ');
            if (m_onChar) m_onChar(L' ');
            return false;
        }
        return false;
    }
    const std::wstring d = KeyDisplay(k);
    if (d.size() == 1) {
        m_text += d[0];
        SendCharDirect(d[0]);
        if (m_onChar) m_onChar(d[0]);
    }
    if (m_shift && !m_caps) m_shift = false;
    return false;
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

// ---------------------------------------------------------------------------
// DrawLiquidTrail — draws a skeuomorphic ink-blob capsule between two points
//
// The effect: a thick rounded capsule whose ends are "circular blobs" and
// whose body tapers/expands based on speed. An outer glow ring and a bright
// specular streak along the top give it a gel/liquid appearance.
// ---------------------------------------------------------------------------
static void DrawLiquidTrail(
    ID2D1RenderTarget* rt,
    ID2D1Factory*      fac,
    float ax, float ay,   // trail end (lagging spring)
    float bx, float by,   // cursor end (leading spring)
    float keyW, float keyH,
    float alpha,
    float ease) noexcept
{
    if (!rt || !fac || alpha < 0.005f) return;
    const float dx   = bx - ax;
    const float dy   = by - ay;
    const float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 2.0f) return;

    // Capsule radius: bigger the further apart the two centres are
    const float minR    = keyH * 0.36f;
    const float maxR    = keyH * 0.52f;
    const float tNorm   = std::min(1.0f, dist / (keyW * 2.8f));
    const float r       = minR + (maxR - minR) * tNorm;
    const float bodyAlpha = alpha * ease;

    // ---- 1. Outer glow (large soft ellipse centred between the two points)
    {
        const float gcx = (ax + bx) * 0.5f;
        const float gcy = (ay + by) * 0.5f;
        const float grx = dist * 0.5f + r * 1.6f;
        const float gry = r * 2.0f;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> gb;
        rt->CreateSolidColorBrush(Tok::GoldWarm(bodyAlpha * 0.18f), gb.GetAddressOf());
        if (gb) rt->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(gcx, gcy), grx, gry), gb.Get());
    }

    // ---- 2. Build a "stadium" (capsule) path geometry
    //    The capsule is a rectangle capped by two semicircles.
    //    We draw it axis-aligned in local space then rotate via transform.
    const float angle  = std::atan2(dy, dx);
    const float cosA   = std::cos(angle);
    const float sinA   = std::sin(angle);

    // Local-space: capsule centre at origin, length along X axis
    // half-length = dist/2, radius = r
    // We approximate with a path: two arcs + two straight edges
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> capsule;
    if (FAILED(fac->CreatePathGeometry(capsule.GetAddressOf())) || !capsule) return;
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> sk;
    capsule->Open(sk.GetAddressOf());
    if (!sk) return;

    auto rot = [&](float lx_, float ly_) -> D2D1_POINT_2F {
        // rotate local point by angle, translate to midpoint between a and b
        const float mx = (ax + bx) * 0.5f;
        const float my = (ay + by) * 0.5f;
        return D2D1::Point2F(
            mx + lx_ * cosA - ly_ * sinA,
            my + lx_ * sinA + ly_ * cosA);
    };

    const float hl = dist * 0.5f; // half-length in local X
    // Start at top of right cap
    sk->BeginFigure(rot(hl, -r), D2D1_FIGURE_BEGIN_FILLED);
    // Right cap arc (clockwise, right semicircle)
    { D2D1_ARC_SEGMENT a{};
      a.point          = rot(hl,  r);
      a.size           = D2D1::SizeF(r, r);
      a.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
      a.arcSize        = D2D1_ARC_SIZE_SMALL;
      sk->AddArc(a); }
    // Bottom edge: right → left
    sk->AddLine(rot(-hl, r));
    // Left cap arc
    { D2D1_ARC_SEGMENT a{};
      a.point          = rot(-hl, -r);
      a.size           = D2D1::SizeF(r, r);
      a.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
      a.arcSize        = D2D1_ARC_SIZE_SMALL;
      sk->AddArc(a); }
    sk->EndFigure(D2D1_FIGURE_END_CLOSED);
    sk->Close();

    // ---- 3. Fill capsule with semi-transparent gold
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fb;
        rt->CreateSolidColorBrush(Tok::GoldMid(bodyAlpha * 0.68f), fb.GetAddressOf());
        if (fb) rt->FillGeometry(capsule.Get(), fb.Get());
    }
    // ---- 4. Gold border (slightly opaque)
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sb;
        rt->CreateSolidColorBrush(Tok::GoldHi(bodyAlpha * 0.82f), sb.GetAddressOf());
        if (sb) rt->DrawGeometry(capsule.Get(), sb.Get(), 1.4f);
    }

    // ---- 5. Specular streak along top 1/3 of the capsule (white highlight)
    //    Drawn as a thinner parallel capsule with reduced radius
    {
        const float sr   = r * 0.38f;
        const float sOff = r * 0.30f; // offset toward "top" in local Y
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> spec;
        if (SUCCEEDED(fac->CreatePathGeometry(spec.GetAddressOf())) && spec) {
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> ssk;
            spec->Open(ssk.GetAddressOf());
            if (ssk) {
                const float shl = hl * 0.65f;
                ssk->BeginFigure(rot(shl, -r + sOff + sr), D2D1_FIGURE_BEGIN_FILLED);
                { D2D1_ARC_SEGMENT a{};
                  a.point          = rot(shl,  -r + sOff - sr);
                  a.size           = D2D1::SizeF(sr, sr);
                  a.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                  a.arcSize        = D2D1_ARC_SIZE_SMALL;
                  ssk->AddArc(a); }
                ssk->AddLine(rot(-shl, -r + sOff - sr));
                { D2D1_ARC_SEGMENT a{};
                  a.point          = rot(-shl, -r + sOff + sr);
                  a.size           = D2D1::SizeF(sr, sr);
                  a.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                  a.arcSize        = D2D1_ARC_SIZE_SMALL;
                  ssk->AddArc(a); }
                ssk->EndFigure(D2D1_FIGURE_END_CLOSED);
                ssk->Close();
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> wb;
                rt->CreateSolidColorBrush(Tok::White(bodyAlpha * 0.28f), wb.GetAddressOf());
                if (wb) rt->FillGeometry(spec.Get(), wb.Get());
            }
        }
    }

    // ---- 6. Dense blob at the trail tail (lagging end)
    {
        const float br = r * 1.05f;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bb;
        rt->CreateSolidColorBrush(Tok::GoldBright(bodyAlpha * 0.55f), bb.GetAddressOf());
        if (bb) rt->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(ax, ay), br, br), bb.Get());
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> br2;
        rt->CreateSolidColorBrush(Tok::GoldHi(bodyAlpha * 0.75f), br2.GetAddressOf());
        if (br2) rt->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(ax, ay), br, br), br2.Get(), 1.2f);
    }
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

    const float kKeyW   = kKeyW_base   * s;
    const float kKeyH   = kKeyH_base   * s;
    const float kGap    = kGap_base    * s;
    const float kCorner = kCorner_base * s;
    const float kPadX   = kPadX_base   * s;
    const float kPadY   = kPadY_base   * s;
    const float kHintH  = kHintH_base  * s;
    const float kAccH   = kAccentH_base * s;

    float maxRowW = 0.0f;
    for (const auto& row : m_rows) {
        float rw = -kGap;
        for (const auto& k : row) rw += k.widthMul * kKeyW + kGap;
        if (rw > maxRowW) maxRowW = rw;
    }
    const float kPanelW = maxRowW + kPadX * 2.0f;
    // Panel height: no text bar, just accent + top padding + rows + hint + bottom padding
    const float kPanelH = kAccH + kPadY
                        + static_cast<float>(m_rows.size()) * (kKeyH + kGap) - kGap
                        + kHintH + kPadY;

    const float targetY = screenH - kPanelH - 28.0f * s;
    const float panelY  = targetY + (1.0f - panelVal) * (screenH - targetY + 28.0f * s);
    const float panelX  = (screenW - kPanelW) * 0.5f;
    const float panelR  = 14.0f * s;

    // Cache geometry for Update's proximity hover
    m_cachedScreenW  = screenW;
    m_cachedScreenH  = screenH;
    m_cachedDpiScale = s;
    m_cachedPanelCX  = panelX + kPanelW * 0.5f;
    m_cachedPanelCY  = panelY + kPanelH * 0.5f;

    // Panel fill
    auto fillPanel = [&](float alpha, bool inner) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceBase(alpha * ease), b.GetAddressOf());
        if (!b) return;
        if (inner) {
            D2D1_ROUNDED_RECT rr{D2D1::RectF(panelX, panelY+kAccH, panelX+kPanelW, panelY+kPanelH), panelR, panelR};
            rt->FillRoundedRectangle(rr, b.Get());
        } else {
            rt->FillRectangle(D2D1::RectF(panelX, panelY+kAccH, panelX+kPanelW, panelY+kAccH+panelR), b.Get());
        }
    };
    { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
      rt->CreateSolidColorBrush(Tok::GoldMid(0.88f * ease), b.GetAddressOf());
      if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(panelX, panelY, panelX+kPanelW, panelY+kAccH+panelR), panelR, panelR};
               rt->FillRoundedRectangle(rr, b.Get()); } }
    fillPanel(0.96f, false);
    fillPanel(0.96f, true);
    { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
      rt->CreateSolidColorBrush(Tok::GoldShadow(0.35f * ease), b.GetAddressOf());
      if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(panelX+0.5f, panelY+0.5f, panelX+kPanelW-0.5f, panelY+kPanelH-0.5f), panelR, panelR};
               rt->DrawRoundedRectangle(rr, b.Get(), 0.9f); } }

    // Hint bar (no text bar above keys anymore)
    {
        const float hy  = panelY + kPanelH - kHintH - kPadY * 0.55f;
        const float hcy = hy + kHintH * 0.5f;
        const float fnt = kFHint_base * s;
        float hx = panelX + kPadX;
        const float chipGap = 8.0f * s;
        DrawHintChip(rt, dwrite, hx, hcy, s, ease,
            L"\u25CF  Type",
            Tok::GoldDeep(0.70f * ease), Tok::GoldHi(0.92f * ease), fnt);
        hx += 84.0f*s + chipGap;
        DrawHintChip(rt, dwrite, hx, hcy, s, ease,
            L"\u25A0  \u232B",
            Tok::SurfaceRaised(0.90f * ease), Tok::ChromeMid(0.80f * ease), fnt);
        hx += 74.0f*s + chipGap;
        DrawHintChip(rt, dwrite, hx, hcy, s, ease,
            L"\u25B2  Submit",
            Tok::GoldMid(0.75f * ease), Tok::GoldBright(0.97f * ease), fnt);
        hx += 94.0f*s + chipGap;
        DrawHintChip(rt, dwrite, hx, hcy, s, ease,
            L"\u25C6  Cancel",
            Tok::SurfaceRaised(0.90f * ease), Tok::ChromeMid(0.80f * ease), fnt);
        hx += 88.0f*s + chipGap;
        {
            const wchar_t* badge = GetLayerName();
            const bool isCyr  = (m_layer == Layer::Cyr);
            const bool isSym  = (m_layer == Layer::Sym);
            const bool isCaps = (!isCyr && !isSym && m_caps);
            DrawHintChip(rt, dwrite, hx, hcy, s, ease, badge,
                isCyr  ? Tok::AmberWarm(0.72f * ease)
                : isSym  ? Tok::GoldDeep(0.85f * ease)
                : isCaps ? Tok::AmberWarm(0.65f * ease)
                :          Tok::SurfaceRaised(0.60f * ease),
                isCyr || isSym || isCaps
                    ? Tok::GoldBright(0.97f * ease)
                    : Tok::ChromeMute(0.60f * ease),
                fnt);
        }
        hx += 64.0f*s + chipGap;
        DrawHintChip(rt, dwrite, hx, hcy, s, ease,
            L"LB  layer",
            Tok::SurfaceRaised(0.55f * ease), Tok::ChromeMute(0.65f * ease), fnt);
    }

    // Keys
    const float keysTop = panelY + kAccH + kPadY;
    const Vec2 selCentre = KeyCentrePixel(m_row, m_col, s, screenW, screenH);
    m_cursorSpringX.target = selCentre.x;
    m_cursorSpringY.target = selCentre.y;
    // Trail spring chases cursor spring (not the target directly)
    m_trailSpringX.target = m_cursorSpringX.value;
    m_trailSpringY.target = m_cursorSpringY.value;
    // Snap on first frame
    if (m_cursorSpringX.value == 0.0f && m_cursorSpringY.value == 0.0f) {
        m_cursorSpringX.Snap(selCentre.x);
        m_cursorSpringY.Snap(selCentre.y);
        m_trailSpringX.Snap(selCentre.x);
        m_trailSpringY.Snap(selCentre.y);
    }
    const float scl = m_cursorScaleSpring.value;

    // -------------------------------------------------------------------------
    // Liquid trail — drawn BEFORE keys so keys render on top
    // -------------------------------------------------------------------------
    if (m_trailAlpha > 0.005f && fac) {
        DrawLiquidTrail(rt, fac.Get(),
            m_trailSpringX.value, m_trailSpringY.value,
            m_cursorSpringX.value, m_cursorSpringY.value,
            kKeyW, kKeyH,
            m_trailAlpha,
            ease);
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
            const float sc2 = sel ? scl : 1.0f;
            const float skw = kw    * sc2;
            const float skh = kKeyH * sc2;
            const float sRx = kCx - skw * 0.5f;
            const float sRy = kCy - skh * 0.5f;
            const float cr  = kCorner * sc2;

            { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
              rt->CreateSolidColorBrush(
                  sel ? Tok::SurfaceRaised(0.97f * ease) : Tok::SurfaceSunken(0.90f * ease),
                  b.GetAddressOf());
              if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(sRx,sRy,sRx+skw,sRy+skh), cr, cr};
                       rt->FillRoundedRectangle(rr, b.Get()); } }
            { const float ins = 2.4f * s;
              Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
              rt->CreateSolidColorBrush(
                  sel ? Tok::SurfaceBase(0.92f*ease) : Tok::SurfaceRaised(0.72f*ease),
                  b.GetAddressOf());
              if (b) { D2D1_ROUNDED_RECT rr{
                  D2D1::RectF(sRx+ins, sRy+ins, sRx+skw-ins, sRy+skh-ins*1.4f),
                  std::max(cr-ins,0.0f), std::max(cr-ins,0.0f)};
                  rt->FillRoundedRectangle(rr, b.Get()); } }
            if (sel) {
                { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                  rt->CreateSolidColorBrush(Tok::GoldHi(0.90f * ease), b.GetAddressOf());
                  if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(sRx,sRy,sRx+skw,sRy+skh), cr, cr};
                           rt->DrawRoundedRectangle(rr, b.Get(), 2.2f*s); } }
                { const float g = 3.0f * s;
                  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                  rt->CreateSolidColorBrush(Tok::GoldWarm(0.18f * ease), b.GetAddressOf());
                  if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(sRx+g,sRy+g,sRx+skw-g,sRy+skh-g),
                           std::max(cr-g,0.0f), std::max(cr-g,0.0f)};
                           rt->DrawRoundedRectangle(rr, b.Get(), 1.0f*s); } }
            } else {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(Tok::InkLine(0.72f * ease), b.GetAddressOf());
                if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(sRx,sRy,sRx+skw,sRy+skh), cr, cr};
                         rt->DrawRoundedRectangle(rr, b.Get(), 0.7f); }
            }
            { const float bi = 1.0f;
              Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
              rt->CreateSolidColorBrush(
                  sel ? Tok::GoldShadow(0.22f*ease) : Tok::GoldDeep(0.08f*ease),
                  b.GetAddressOf());
              if (b) { D2D1_ROUNDED_RECT rr{
                  D2D1::RectF(sRx+bi, sRy+bi, sRx+skw-bi, sRy+skh-bi),
                  std::max(cr-bi,0.0f), std::max(cr-bi,0.0f)};
                  rt->DrawRoundedRectangle(rr, b.Get(), 0.5f); } }

            if (fac) DrawArcSpecular(rt, fac.Get(),
                kCx, sRy + skh * 0.28f, skw * 0.26f, skh * 0.18f,
                (sel ? 0.10f : 0.04f) * ease);

            if (dwrite) {
                const std::wstring disp = KeyDisplay(k);
                if (!disp.empty()) {
                    const float fs = (k.isSpecial ? kFSpec_base : kFKey_base) * s * sc2;
                    const wchar_t* fontFace = k.isSpecial ? L"Segoe UI Symbol" : L"Segoe UI";
                    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
                    dwrite->CreateTextFormat(fontFace, nullptr,
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
    (void)keysTop; // used above
}

} // namespace enjoystick::overlay
