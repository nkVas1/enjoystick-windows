#include <enjoystick/overlay/RadialMenu.hpp>
#include "Overlay_Theme.hpp"

#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

#include <cmath>
#include <algorithm>

#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif
static constexpr float kPi  = static_cast<float>(M_PI);
static constexpr float k2Pi = static_cast<float>(2.0 * M_PI);

namespace enjoystick::overlay {

// Flash duration for confirmation burst (ms)
static constexpr float kFlashMs = 130.0f;

// ---------------------------------------------------------------------------
// Easing
// ---------------------------------------------------------------------------
static float BounceEaseOut(float t) noexcept {
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * std::cos(t * 11.0f);
}
static float EaseInQuad(float t) noexcept { return t * t; }

// Exponential ease: flat for first ~60%, then rapid acceleration.
// Returns 0 at t=0, 1 at t=1.
static float EaseInExpo(float t) noexcept {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return std::pow(2.0f, 10.0f * t - 10.0f);
}

// ---------------------------------------------------------------------------
// Top-edge arc stroke specular
// ---------------------------------------------------------------------------
static void DrawArcSpecular(
    ID2D1RenderTarget* rt,
    ID2D1Factory*      fac,
    float cx, float cy, float r,
    float alpha) noexcept
{
    if (!rt || !fac || r < 2.0f || alpha < 0.004f) return;
    const float aFrom = -kPi * 0.76f;
    const float aTo   = -kPi * 0.24f;
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> g;
    fac->CreatePathGeometry(g.GetAddressOf());
    if (!g) return;
    Microsoft::WRL::ComPtr<ID2D1GeometrySink> s;
    g->Open(s.GetAddressOf());
    if (!s) return;
    const float ar = r - 1.5f;
    s->BeginFigure(
        D2D1::Point2F(cx + ar * std::cos(aFrom), cy + ar * std::sin(aFrom)),
        D2D1_FIGURE_BEGIN_HOLLOW);
    D2D1_ARC_SEGMENT arc{};
    arc.point          = D2D1::Point2F(cx + ar * std::cos(aTo), cy + ar * std::sin(aTo));
    arc.size           = D2D1::SizeF(ar, ar);
    arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
    arc.arcSize        = D2D1_ARC_SIZE_SMALL;
    s->AddArc(arc);
    s->EndFigure(D2D1_FIGURE_END_OPEN);
    s->Close();
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
    rt->CreateSolidColorBrush(Tok::White(alpha), b.GetAddressOf());
    if (b) rt->DrawGeometry(g.Get(), b.Get(), 0.9f);
}

// ---------------------------------------------------------------------------
// RadialMenu
// ---------------------------------------------------------------------------
RadialMenu::RadialMenu(Config config) : m_config(std::move(config)) {}
RadialMenu::~RadialMenu() = default;

void RadialMenu::SetItems(std::vector<RadialMenuItem> items) {
    m_items        = std::move(items);
    m_hoveredIndex = m_latchedIndex = -1;
    m_itemScales.assign(m_items.size(), 0.0f);
    m_itemDelays.resize(m_items.size());
    for (size_t i = 0; i < m_items.size(); ++i)
        m_itemDelays[i] = static_cast<float>(i) * 0.008f;
}

bool              RadialMenu::IsVisible()      const noexcept { return m_state != State::Hidden; }
RadialMenu::State RadialMenu::GetState()        const noexcept { return m_state; }
int32_t           RadialMenu::GetHoveredIndex() const noexcept { return m_hoveredIndex; }

void RadialMenu::Open() {
    if (m_state == State::Visible || m_state == State::Opening) return;
    m_state        = State::Opening;
    m_animTime     = 0.0f;
    m_scrimAlpha   = 0.0f;
    m_hoveredIndex = m_latchedIndex = m_confirmedIndex = -1;
    m_latchTimer   = 0.0f;
    m_glowPhase    = 0.0f;
    m_flashTimer   = 0.0f;
    m_flashIndex   = -1;
    m_dwellTimer   = 0.0f;
    m_dwellSector  = -1;
    m_stickInDeadzone = true;
    for (auto& sc : m_itemScales) sc = 0.0f;
    m_itemDelayTimers.assign(m_items.size(), 0.0f);
}
void RadialMenu::Close() {
    if (m_state == State::Hidden || m_state == State::Closing) return;
    m_state    = State::Closing;
    m_animTime = 0.0f;
}

void RadialMenu::Update(const ControllerState& state, float deltaSeconds) {
    // Advance flash timer if active; close after flash completes
    if (m_flashIndex >= 0) {
        m_flashTimer += deltaSeconds * 1000.0f;
        if (m_flashTimer >= kFlashMs) {
            m_flashIndex = -1;
            m_flashTimer = 0.0f;
            if (m_state != State::Closing && m_state != State::Hidden)
                Close();
        }
        UpdateAnimation(deltaSeconds);
        return;
    }

    UpdateAnimation(deltaSeconds);
    if (m_state == State::Hidden) {
        m_prevSouth = m_prevEast = false;
        m_latchedIndex = -1;
        m_latchTimer   = 0.0f;
        m_dwellTimer   = 0.0f;
        m_dwellSector  = -1;
        m_stickInDeadzone = true;
        return;
    }
    // UpdateSelection now owns dwell accumulation and auto-confirm.
    UpdateSelection(state.rightStick, deltaSeconds);

    const bool south = HasButton(state.buttons, Button::South);
    const bool east  = HasButton(state.buttons, Button::East);
    if (south && !m_prevSouth) ConfirmSelection();
    if (east  && !m_prevEast)  Close();
    m_prevSouth = south;
    m_prevEast  = east;
}

void RadialMenu::UpdateAnimation(float deltaSeconds) {
    const float openMs  = m_config.openAnimMs;
    const float closeMs = m_config.closeAnimMs;

    m_glowPhase += deltaSeconds * k2Pi * 2.0f;
    if (m_glowPhase > k2Pi) m_glowPhase -= k2Pi;

    if (m_state == State::Opening) {
        m_animTime += deltaSeconds * 1000.0f;
        const float t  = std::min(1.0f, m_animTime / openMs);
        m_animProgress = BounceEaseOut(t);
        m_scrimAlpha   = t;
        for (size_t i = 0; i < m_itemScales.size(); ++i) {
            const float delay  = m_itemDelays[i];
            const float localT = std::max(0.0f,
                std::min(1.0f, (m_animTime * 0.001f - delay) / (openMs * 0.001f - delay + 0.001f)));
            m_itemScales[i] = BounceEaseOut(localT);
        }
        if (t >= 1.0f) {
            m_animProgress = 1.0f; m_scrimAlpha = 1.0f;
            m_state = State::Visible;
            for (auto& sc : m_itemScales) sc = 1.0f;
            if (m_onOpen) m_onOpen();
        }
    } else if (m_state == State::Closing) {
        m_animTime += deltaSeconds * 1000.0f;
        const float t  = std::min(1.0f, m_animTime / closeMs);
        m_animProgress = 1.0f - EaseInQuad(t);
        m_scrimAlpha   = 1.0f - t;
        for (auto& sc : m_itemScales) sc = m_animProgress;
        if (t >= 1.0f) {
            m_animProgress = 0.0f; m_scrimAlpha = 0.0f;
            m_state = State::Hidden;
            if (m_onClose) m_onClose();
        }
    }
}

void RadialMenu::UpdateSelection(Vec2 stick, float deltaSeconds) {
    if (m_items.empty()) return;
    const float magSq = stick.LengthSq();
    const float dzSq  = m_config.selectionDeadzone * m_config.selectionDeadzone;
    if (magSq < dzSq) {
        m_stickInDeadzone = true;
        if (m_latchedIndex >= 0) {
            m_latchTimer += deltaSeconds * 1000.0f;
            if (m_latchTimer >= m_config.latchMs) { m_latchedIndex = -1; m_hoveredIndex = -1; }
            else                                   m_hoveredIndex  = m_latchedIndex;
        } else { m_hoveredIndex = -1; }
        // Stick returned to centre: dwell resets
        m_dwellTimer  = 0.0f;
        m_dwellSector = -1;
        return;
    }
    m_latchTimer = 0.0f;

    float angle = std::atan2(-stick.y, stick.x);
    if (angle < 0.0f) angle += k2Pi;

    const float   sector   = k2Pi / static_cast<float>(m_items.size());
    const float   adjusted = std::fmod(angle + kPi * 0.5f, k2Pi);
    const int32_t idx      = static_cast<int32_t>(adjusted / sector) %
                             static_cast<int32_t>(m_items.size());

    const bool sectorChanged    = (idx != m_hoveredIndex);
    const bool justLeftDeadzone = m_stickInDeadzone;
    m_stickInDeadzone = false;

    if (sectorChanged || justLeftDeadzone) {
        m_glowPhase   = 0.0f;
        m_dwellTimer  = 0.0f;
        m_dwellSector = idx;
    } else {
        if (m_dwellSector < 0) m_dwellSector = idx;
    }
    m_hoveredIndex = idx;
    m_latchedIndex = idx;

    // -----------------------------------------------------------------------
    // Dwell accumulation (owned here so sector is always authoritative)
    // -----------------------------------------------------------------------
    if (m_dwellSector == idx) {
        m_dwellTimer += deltaSeconds * 1000.0f;
        if (m_dwellTimer >= kDwellConfirmMs) {
            // Auto-confirm: act as if the user pressed South on this sector
            m_dwellTimer  = 0.0f;
            m_dwellSector = -1;
            ConfirmSelection();
        }
    }
}

void RadialMenu::ConfirmSelection() {
    const int32_t target = (m_hoveredIndex >= 0) ? m_hoveredIndex : m_latchedIndex;
    if (target < 0 || target >= static_cast<int32_t>(m_items.size())) return;
    m_confirmedIndex = target;
    if (const auto& a = m_items[static_cast<size_t>(m_confirmedIndex)].action) a();
    m_flashIndex = m_confirmedIndex;
    m_flashTimer = 0.0f;
}

float RadialMenu::AngleForIndex(int32_t index) const noexcept {
    return -kPi * 0.5f + static_cast<float>(index) *
           (k2Pi / static_cast<float>(m_items.size()));
}
Vec2 RadialMenu::PositionForIndex(int32_t index, float cx, float cy, float radius) const noexcept {
    const float a = AngleForIndex(index);
    return { cx + radius * std::cos(a), cy + radius * std::sin(a) };
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------
void RadialMenu::Draw(
    void*  renderTargetPtr,
    void*  dwriteFactoryPtr,
    float  dpiScale,
    float  screenW,
    float  screenH) const
{
    if (m_state == State::Hidden && m_flashIndex < 0 && m_animProgress <= 0.0f) return;
    if (!renderTargetPtr) return;

    auto* rt     = static_cast<ID2D1RenderTarget*>(renderTargetPtr);
    auto* dwrite = static_cast<IDWriteFactory*>(dwriteFactoryPtr);

    const float sc    = std::max(0.0f, m_animProgress);
    const float alpha = std::min(1.0f, std::max(0.0f, m_scrimAlpha));
    const float cx    = (m_config.centreX < 0) ? screenW * 0.5f : static_cast<float>(m_config.centreX);
    const float cy    = (m_config.centreY < 0) ? screenH * 0.5f : static_cast<float>(m_config.centreY);

    const float configRadius = (m_config.radius > 0) ? m_config.radius : 220.0f;
    const float radius  = configRadius * std::min(sc, 1.0f) * dpiScale;
    const float itemR   = 52.0f * std::min(sc, 1.0f) * dpiScale;
    const float discR   = radius * 0.44f;
    const float s       = dpiScale;
    const int32_t displayHovered = m_hoveredIndex;

    // -----------------------------------------------------------------------
    // Dwell progress and EXPONENTIAL tremor intensity
    // dwellFrac: 0..1 over the full dwell window (after kDwellStartMs)
    // dwellEased: exponential ramp -- flat early, explosive near confirm
    // -----------------------------------------------------------------------
    const float dwellFrac = (m_dwellTimer > kDwellStartMs)
        ? std::min(1.0f, (m_dwellTimer - kDwellStartMs) / (kDwellConfirmMs - kDwellStartMs))
        : 0.0f;
    const float dwellEased = EaseInExpo(dwellFrac); // exponential, not cubic

    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    rt->GetFactory(factory.GetAddressOf());

    // ---- Scrim
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::Scrim(0.55f * alpha), b.GetAddressOf());
        if (b) rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, screenW, screenH), b.Get());
    }

    // ---- Outer rings
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldShadow(0.38f * alpha), b.GetAddressOf());
        if (b) rt->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(cx,cy), radius + itemR*0.84f, radius + itemR*0.84f),
            b.Get(), 1.6f * s);
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldShadow(0.20f * alpha), b.GetAddressOf());
        if (b) rt->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(cx,cy), radius + itemR*0.54f, radius + itemR*0.54f),
            b.Get(), 0.8f * s);
    }

    // ---- Dividers
    if (factory && m_items.size() > 1) {
        const int32_t n      = static_cast<int32_t>(m_items.size());
        const float   outerR = radius + itemR * 0.54f;
        for (int32_t i = 0; i < n; ++i) {
            const float a = AngleForIndex(i) - (k2Pi / static_cast<float>(n)) * 0.5f;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(Tok::GoldShadow(0.20f * alpha), b.GetAddressOf());
            if (b) rt->DrawLine(
                D2D1::Point2F(cx + discR * std::cos(a),  cy + discR * std::sin(a)),
                D2D1::Point2F(cx + outerR * std::cos(a), cy + outerR * std::sin(a)),
                b.Get(), 0.8f * s);
        }
    }

    // ---- Sector highlight
    if (displayHovered >= 0 && m_items.size() > 1 && factory) {
        const float latchFrac = (m_config.latchMs > 0.0f)
            ? std::min(1.0f, m_latchTimer / m_config.latchMs) : 0.0f;
        const float latchPulse = (latchFrac > 0.0f)
            ? 0.55f + 0.20f * std::sin(latchFrac * kPi * 4.0f) : 0.72f;
        const int32_t n      = static_cast<int32_t>(m_items.size());
        const float   sector = k2Pi / static_cast<float>(n);
        const float   startA = AngleForIndex(displayHovered) - sector * 0.5f;
        const float   endA   = startA + sector;
        const float   outerR = radius + itemR * 0.62f;
        const float   cosS = std::cos(startA), sinS = std::sin(startA);
        const float   cosE = std::cos(endA),   sinE = std::sin(endA);

        Microsoft::WRL::ComPtr<ID2D1PathGeometry> geom;
        factory->CreatePathGeometry(geom.GetAddressOf());
        if (geom) {
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
            geom->Open(sink.GetAddressOf());
            if (sink) {
                sink->BeginFigure(D2D1::Point2F(cx + discR*cosS, cy + discR*sinS), D2D1_FIGURE_BEGIN_FILLED);
                D2D1_ARC_SEGMENT ia{};
                ia.point = D2D1::Point2F(cx + discR*cosE, cy + discR*sinE);
                ia.size  = D2D1::SizeF(discR,discR);
                ia.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                ia.arcSize = (sector > kPi) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
                sink->AddArc(ia);
                sink->AddLine(D2D1::Point2F(cx + outerR*cosE, cy + outerR*sinE));
                D2D1_ARC_SEGMENT oa{};
                oa.point = D2D1::Point2F(cx + outerR*cosS, cy + outerR*sinS);
                oa.size  = D2D1::SizeF(outerR,outerR);
                oa.sweepDirection = D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
                oa.arcSize = (sector > kPi) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
                sink->AddArc(oa);
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
                // Fill alpha boosted exponentially at high dwell
                const float fillAlpha = latchPulse * (0.58f + 0.24f * dwellEased) * alpha;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fill;
                rt->CreateSolidColorBrush(Tok::SurfaceRaised(fillAlpha), fill.GetAddressOf());
                if (fill) rt->FillGeometry(geom.Get(), fill.Get());

                Microsoft::WRL::ComPtr<ID2D1PathGeometry> bg;
                factory->CreatePathGeometry(bg.GetAddressOf());
                if (bg) {
                    Microsoft::WRL::ComPtr<ID2D1GeometrySink> bs;
                    bg->Open(bs.GetAddressOf());
                    if (bs) {
                        bs->BeginFigure(D2D1::Point2F(cx + outerR*cosS, cy + outerR*sinS), D2D1_FIGURE_BEGIN_HOLLOW);
                        D2D1_ARC_SEGMENT ba{};
                        ba.point = D2D1::Point2F(cx + outerR*cosE, cy + outerR*sinE);
                        ba.size  = D2D1::SizeF(outerR, outerR);
                        ba.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                        ba.arcSize = (sector > kPi) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
                        bs->AddArc(ba);
                        bs->EndFigure(D2D1_FIGURE_END_OPEN);
                        bs->Close();
                        const float borderAlpha = (0.90f + 0.10f * dwellEased) * alpha;
                        // Border stroke width ramps from 2.2 to 5.0 exponentially
                        const float strokeW = (2.2f + 2.8f * dwellEased) * s;
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> gb;
                        rt->CreateSolidColorBrush(Tok::GoldMid(borderAlpha), gb.GetAddressOf());
                        if (gb) rt->DrawGeometry(bg.Get(), gb.Get(), strokeW);
                    }
                }
            }
        }
    }

    // ---- Centre disc
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceBase(0.90f * alpha), b.GetAddressOf());
        if (b) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy), discR, discR), b.Get());
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceSunken(0.94f * alpha), b.GetAddressOf());
        if (b) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy), discR * 0.66f, discR * 0.66f), b.Get());
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldMid(0.64f * alpha), b.GetAddressOf());
        if (b) rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy), discR, discR), b.Get(), 1.8f * s);
    }
    if (factory) DrawArcSpecular(rt, factory.Get(), cx, cy, discR, 0.08f * alpha);

    if (m_items.empty()) return;

    const float glowBreathe = 0.5f + 0.5f * std::sin(m_glowPhase);

    // ---- Item circles
    for (int32_t i = 0; i < static_cast<int32_t>(m_items.size()); ++i) {
        const float itemSc = (i < static_cast<int32_t>(m_itemScales.size()))
                             ? std::min(1.2f, m_itemScales[static_cast<size_t>(i)]) : sc;
        const Vec2   pos   = PositionForIndex(i, cx, cy, radius);
        const bool   hovered = (i == displayHovered);
        const float  px = pos.x, py = pos.y;

        // Dwell tremor: exponential ramp in frequency AND amplitude.
        // Phase is seeded from glowPhase (which advances at 2*2pi/s) so the
        // oscillation is smooth and doesn’t snap on dwell start.
        float tremorX = 0.0f, tremorY = 0.0f;
        if (hovered && dwellEased > 0.0f) {
            // Frequency: 5 Hz at dwell start -> 32 Hz at confirm (exponential feel)
            const float freq = 5.0f + dwellEased * 27.0f;
            // Amplitude: 0 -> 6 px
            const float amp  = dwellEased * 6.0f * dpiScale;
            const float phase = m_glowPhase * freq * 0.5f;
            tremorX = amp * std::sin(phase * 2.31f);
            tremorY = amp * std::cos(phase * 1.87f);
        }

        const float iR = itemR * itemSc;
        if (iR <= 0.0f) continue;

        const float ipx = px + tremorX;
        const float ipy = py + tremorY;

        // Glow rings for hovered
        if (hovered) {
            const float glowExtra = dwellEased * 0.32f;
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> g;
                rt->CreateSolidColorBrush(Tok::GoldGlow((0.32f + 0.15f * glowBreathe + 0.26f * dwellEased) * alpha), g.GetAddressOf());
                if (g) rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(ipx,ipy),
                    iR*(1.50f + 0.12f*glowBreathe + glowExtra), iR*(1.50f + 0.12f*glowBreathe + glowExtra)), g.Get(), (4.5f + 3.5f*dwellEased)*s);
            }
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> g2;
                rt->CreateSolidColorBrush(Tok::GoldGlow((0.22f + 0.18f * dwellEased) * alpha), g2.GetAddressOf());
                if (g2) rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(ipx,ipy),
                    iR*1.22f, iR*1.22f), g2.Get(), 2.0f*s);
            }
        }
        { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
          rt->CreateSolidColorBrush(Tok::SurfaceSunken(0.92f*alpha), b.GetAddressOf());
          if (b) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ipx,ipy),iR,iR),b.Get()); }
        { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
          rt->CreateSolidColorBrush(hovered?Tok::SurfaceRaised(0.74f*alpha):Tok::SurfaceBase(0.60f*alpha),b.GetAddressOf());
          if (b) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ipx,ipy),iR*0.82f,iR*0.82f),b.Get()); }
        { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
          rt->CreateSolidColorBrush(hovered?Tok::GoldHi(0.96f*alpha):Tok::GoldMid(0.48f*alpha),b.GetAddressOf());
          if (b) rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(ipx,ipy),iR,iR),b.Get(),hovered?2.2f*s:1.1f*s); }
        if (hovered) {
            const float bevelR = iR - 2.5f*s;
            if (bevelR > 0.0f) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bv;
                rt->CreateSolidColorBrush(Tok::GoldWarm(0.32f*alpha),bv.GetAddressOf());
                if (bv) rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(ipx,ipy),bevelR,bevelR),bv.Get(),1.0f*s);
            }
        }
        if (factory) DrawArcSpecular(rt,factory.Get(),ipx,ipy,iR,(hovered?0.10f:0.05f)*alpha);

        if (!dwrite) continue;
        const std::wstring& iconStr = m_items[static_cast<size_t>(i)].icon;
        if (!iconStr.empty()) {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
            dwrite->CreateTextFormat(L"Segoe UI Emoji",nullptr,DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,26.0f*itemSc*s,L"en-us",fmt.GetAddressOf());
            if (fmt) {
                fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tb;
                rt->CreateSolidColorBrush(hovered?Tok::GoldHi(alpha):Tok::ChromeMid(0.90f*alpha),tb.GetAddressOf());
                if (tb) rt->DrawText(iconStr.c_str(),static_cast<UINT32>(iconStr.size()),
                    fmt.Get(),D2D1::RectF(ipx-iR,ipy-iR,ipx+iR,ipy+iR),tb.Get());
            }
        }
        const std::wstring& labelStr = m_items[static_cast<size_t>(i)].label;
        if (!labelStr.empty()) {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> lf;
            dwrite->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,15.0f*itemSc*s,L"en-us",lf.GetAddressOf());
            if (lf) {
                lf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                lf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> lb;
                rt->CreateSolidColorBrush(hovered?Tok::GoldHi(alpha):Tok::ChromeMute(0.82f*alpha),lb.GetAddressOf());
                if (lb) {
                    const float lw = 100.0f*itemSc*s;
                    rt->DrawText(labelStr.c_str(),static_cast<UINT32>(labelStr.size()),lf.Get(),
                        D2D1::RectF(ipx-lw*0.5f,ipy+iR+5.0f*itemSc*s,ipx+lw*0.5f,ipy+iR+26.0f*itemSc*s),lb.Get());
                }
            }
        }

        // ---- Confirmation flash overlay
        if (i == m_flashIndex && m_flashTimer > 0.0f) {
            const float ft  = std::min(1.0f, m_flashTimer / kFlashMs);
            const float env = (ft < 0.5f) ? ft * 2.0f : 1.0f - (ft - 0.5f) * 2.0f;
            const float flashAlpha = env * 0.85f;
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fb;
                rt->CreateSolidColorBrush(Tok::White(flashAlpha), fb.GetAddressOf());
                if (fb) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(ipx,ipy),
                    iR * 1.10f, iR * 1.10f), fb.Get());
            }
            {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fb2;
                rt->CreateSolidColorBrush(Tok::GoldBright(flashAlpha * 0.55f), fb2.GetAddressOf());
                if (fb2) rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(ipx,ipy),
                    iR * 1.28f, iR * 1.28f), fb2.Get(), 3.0f * s);
            }
        }
    }

    // ---- Centre label
    if (dwrite && displayHovered >= 0) {
        const wchar_t* hint = m_items[static_cast<size_t>(displayHovered)].label.c_str();
        if (hint && hint[0] != L'\0') {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> hf;
            dwrite->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_BOLD,
                DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,18.0f*sc*s,L"en-us",hf.GetAddressOf());
            if (hf) {
                hf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                hf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hb;
                rt->CreateSolidColorBrush(Tok::GoldHi(alpha), hb.GetAddressOf());
                if (hb) rt->DrawText(hint,static_cast<UINT32>(std::wcslen(hint)),hf.Get(),
                    D2D1::RectF(cx-discR,cy-discR*0.6f,cx+discR,cy+discR*0.2f),hb.Get());
            }
        }
        // Dwell progress arc in centre disc
        if (dwellEased > 0.0f && factory) {
            const float progressA = dwellFrac * k2Pi; // progress arc uses linear frac, not eased
            const float startA2   = -kPi * 0.5f;
            const float endA2     = startA2 + progressA;
            const float pR        = discR * 0.62f;
            Microsoft::WRL::ComPtr<ID2D1PathGeometry> pg;
            factory->CreatePathGeometry(pg.GetAddressOf());
            if (pg) {
                Microsoft::WRL::ComPtr<ID2D1GeometrySink> ps;
                pg->Open(ps.GetAddressOf());
                if (ps) {
                    ps->BeginFigure(
                        D2D1::Point2F(cx + pR * std::cos(startA2), cy + pR * std::sin(startA2)),
                        D2D1_FIGURE_BEGIN_HOLLOW);
                    D2D1_ARC_SEGMENT pa{};
                    pa.point = D2D1::Point2F(cx + pR * std::cos(endA2), cy + pR * std::sin(endA2));
                    pa.size  = D2D1::SizeF(pR, pR);
                    pa.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                    pa.arcSize = (progressA > kPi) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
                    ps->AddArc(pa);
                    ps->EndFigure(D2D1_FIGURE_END_OPEN);
                    ps->Close();
                    // Progress arc brightness also scales exponentially with dwell
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> pb;
                    rt->CreateSolidColorBrush(Tok::GoldBright((0.65f + 0.35f * dwellEased) * alpha), pb.GetAddressOf());
                    if (pb) rt->DrawGeometry(pg.Get(), pb.Get(), (2.5f + 1.5f * dwellEased) * s);
                }
            }
        }

        const wchar_t* hintLine = (dwellEased > 0.0f)
            ? L"\u25CF Hold to confirm..."
            : L"\u25CF Confirm  \u25C6 Cancel";
        Microsoft::WRL::ComPtr<IDWriteTextFormat> sf;
        dwrite->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,12.0f*sc*s,L"en-us",sf.GetAddressOf());
        if (sf) {
            sf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            sf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sb;
            rt->CreateSolidColorBrush(Tok::ChromeMute(0.55f*alpha),sb.GetAddressOf());
            if (sb) rt->DrawText(hintLine,static_cast<UINT32>(std::wcslen(hintLine)),sf.Get(),
                D2D1::RectF(cx-discR,cy+discR*0.22f,cx+discR,cy+discR*0.80f),sb.Get());
        }
    }
}

} // namespace enjoystick::overlay
