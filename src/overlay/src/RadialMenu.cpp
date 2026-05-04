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

// ---------------------------------------------------------------------------
// Easing
// ---------------------------------------------------------------------------
static float BounceEaseOut(float t) noexcept {
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * std::cos(t * 11.0f);
}
static float EaseInQuad(float t) noexcept { return t * t; }

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
    for (auto& sc : m_itemScales) sc = 0.0f;
    m_itemDelayTimers.assign(m_items.size(), 0.0f);
}
void RadialMenu::Close() {
    if (m_state == State::Hidden || m_state == State::Closing) return;
    m_state    = State::Closing;
    m_animTime = 0.0f;
}

void RadialMenu::Update(const ControllerState& state, float deltaSeconds) {
    UpdateAnimation(deltaSeconds);
    if (m_state == State::Hidden) {
        m_prevSouth = m_prevEast = false;
        m_latchedIndex = -1;
        m_latchTimer   = 0.0f;
        return;
    }
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
        if (m_latchedIndex >= 0) {
            m_latchTimer += deltaSeconds * 1000.0f;
            if (m_latchTimer >= m_config.latchMs) { m_latchedIndex = -1; m_hoveredIndex = -1; }
            else                                   m_hoveredIndex  = m_latchedIndex;
        } else { m_hoveredIndex = -1; }
        return;
    }
    m_latchTimer = 0.0f;
    float angle = std::atan2(stick.y, stick.x);
    if (angle < 0.0f) angle += k2Pi;
    const float   sector   = k2Pi / static_cast<float>(m_items.size());
    const float   adjusted = std::fmod(angle + kPi * 0.5f, k2Pi);
    const int32_t idx      = static_cast<int32_t>(adjusted / sector) %
                             static_cast<int32_t>(m_items.size());
    if (idx != m_hoveredIndex) m_glowPhase = 0.0f;
    m_hoveredIndex = idx;
    m_latchedIndex = idx;
}

void RadialMenu::ConfirmSelection() {
    const int32_t target = (m_hoveredIndex >= 0) ? m_hoveredIndex : m_latchedIndex;
    if (target < 0 || target >= static_cast<int32_t>(m_items.size())) return;
    m_confirmedIndex = target;
    if (const auto& a = m_items[static_cast<size_t>(m_confirmedIndex)].action) a();
    Close();
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
// Draw  — Futurist Glamour  (v2 — no oval artefacts)
// ---------------------------------------------------------------------------
void RadialMenu::Draw(
    void*  renderTargetPtr,
    void*  dwriteFactoryPtr,
    float  dpiScale,
    float  screenW,
    float  screenH) const
{
    if (m_state == State::Hidden || m_animProgress <= 0.0f || !renderTargetPtr) return;

    auto* rt     = static_cast<ID2D1RenderTarget*>(renderTargetPtr);
    auto* dwrite = static_cast<IDWriteFactory*>(dwriteFactoryPtr);

    const float sc    = std::max(0.0f, m_animProgress);
    const float alpha = std::min(1.0f, std::max(0.0f, m_scrimAlpha));
    const float cx    = (m_config.centreX < 0) ? screenW * 0.5f : static_cast<float>(m_config.centreX);
    const float cy    = (m_config.centreY < 0) ? screenH * 0.5f : static_cast<float>(m_config.centreY);
    const float radius = m_config.radius * std::min(sc, 1.0f) * dpiScale;
    const float discR  = radius * 0.40f;
    const float itemR  = 32.0f * std::min(sc, 1.0f) * dpiScale;
    const float s      = dpiScale;
    const int32_t displayHovered = m_hoveredIndex;

    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    rt->GetFactory(factory.GetAddressOf());

    // ---- Full-screen scrim --------------------------------------------------
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::DeepVoid(0.52f * alpha), b.GetAddressOf());
        if (b) rt->FillRectangle(D2D1::RectF(0.0f, 0.0f, screenW, screenH), b.Get());
    }

    // ---- Outer ornamental gold ring -----------------------------------------
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldShadow(0.35f * alpha), b.GetAddressOf());
        if (b) rt->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(cx,cy), radius + itemR*0.82f, radius + itemR*0.82f),
            b.Get(), 1.2f * s);
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldGlow(0.20f * alpha), b.GetAddressOf());
        if (b) rt->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(cx,cy), radius + itemR*0.52f, radius + itemR*0.52f),
            b.Get(), 0.6f * s);
    }

    // ---- Sector divider rays ------------------------------------------------
    if (factory && m_items.size() > 1) {
        const int32_t n      = static_cast<int32_t>(m_items.size());
        const float   outerR = radius + itemR * 0.52f;
        for (int32_t i = 0; i < n; ++i) {
            const float a = AngleForIndex(i) - (k2Pi / static_cast<float>(n)) * 0.5f;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(Tok::GoldShadow(0.18f * alpha), b.GetAddressOf());
            if (b) rt->DrawLine(
                D2D1::Point2F(cx + discR * std::cos(a),  cy + discR * std::sin(a)),
                D2D1::Point2F(cx + outerR * std::cos(a), cy + outerR * std::sin(a)),
                b.Get(), 0.7f * s);
        }
    }

    // ---- Sector highlight (selected wedge) ----------------------------------
    if (displayHovered >= 0 && m_items.size() > 1 && factory) {
        const float latchFrac = (m_config.latchMs > 0.0f)
            ? std::min(1.0f, m_latchTimer / m_config.latchMs) : 0.0f;
        const float latchPulse = (latchFrac > 0.0f)
            ? 0.55f + 0.20f * std::sin(latchFrac * kPi * 4.0f) : 0.72f;
        const int32_t n      = static_cast<int32_t>(m_items.size());
        const float   sector = k2Pi / static_cast<float>(n);
        const float   startA = AngleForIndex(displayHovered) - sector * 0.5f;
        const float   endA   = startA + sector;
        const float   outerR = radius + itemR * 0.60f;
        const float   cosS = std::cos(startA), sinS = std::sin(startA);
        const float   cosE = std::cos(endA),   sinE = std::sin(endA);

        Microsoft::WRL::ComPtr<ID2D1PathGeometry> geom;
        factory->CreatePathGeometry(geom.GetAddressOf());
        if (geom) {
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
            geom->Open(sink.GetAddressOf());
            if (sink) {
                sink->BeginFigure(
                    D2D1::Point2F(cx + discR*cosS, cy + discR*sinS),
                    D2D1_FIGURE_BEGIN_FILLED);
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

                // Lit facet fill: SurfaceRaised with subtle gold tint
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fill;
                rt->CreateSolidColorBrush(
                    Tok::SurfaceRaised(latchPulse * 0.55f * alpha), fill.GetAddressOf());
                if (fill) rt->FillGeometry(geom.Get(), fill.Get());

                // Gold outer arc border
                Microsoft::WRL::ComPtr<ID2D1PathGeometry> borderGeom;
                factory->CreatePathGeometry(borderGeom.GetAddressOf());
                if (borderGeom) {
                    Microsoft::WRL::ComPtr<ID2D1GeometrySink> bs;
                    borderGeom->Open(bs.GetAddressOf());
                    if (bs) {
                        bs->BeginFigure(
                            D2D1::Point2F(cx + outerR*cosS, cy + outerR*sinS),
                            D2D1_FIGURE_BEGIN_HOLLOW);
                        D2D1_ARC_SEGMENT ba{};
                        ba.point = D2D1::Point2F(cx + outerR*cosE, cy + outerR*sinE);
                        ba.size  = D2D1::SizeF(outerR, outerR);
                        ba.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                        ba.arcSize = (sector > kPi) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
                        bs->AddArc(ba);
                        bs->EndFigure(D2D1_FIGURE_END_OPEN);
                        bs->Close();
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> gb;
                        rt->CreateSolidColorBrush(Tok::GoldMid(0.88f * alpha), gb.GetAddressOf());
                        if (gb) rt->DrawGeometry(borderGeom.Get(), gb.Get(), 1.8f * s);
                    }
                }
            }
        }
    }

    // ---- Background disc — two solid fills (sunken core + base rim) ---------
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceBase(0.88f * alpha), b.GetAddressOf());
        if (b) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy), discR, discR), b.Get());
    }
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceSunken(0.92f * alpha), b.GetAddressOf());
        if (b) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy), discR * 0.68f, discR * 0.68f), b.Get());
    }
    // Gold ring around centre disc
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldMid(0.60f * alpha), b.GetAddressOf());
        if (b) rt->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(cx,cy), discR, discR), b.Get(), 1.4f * s);
    }
    // Top-edge luminance arc on disc (replaces oval blob)
    // A short arc stroke from ~320° to ~40° at discR - 1px, 4% white.
    if (factory) {
        const float arcR  = discR - 1.2f * s;
        const float aFrom = -kPi * 0.72f; // ~-130° (upper-left)
        const float aTo   = -kPi * 0.28f; // ~-50°  (upper-right)
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> arcGeom;
        factory->CreatePathGeometry(arcGeom.GetAddressOf());
        if (arcGeom) {
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
            arcGeom->Open(sink.GetAddressOf());
            if (sink) {
                sink->BeginFigure(
                    D2D1::Point2F(cx + arcR * std::cos(aFrom), cy + arcR * std::sin(aFrom)),
                    D2D1_FIGURE_BEGIN_HOLLOW);
                D2D1_ARC_SEGMENT arc{};
                arc.point         = D2D1::Point2F(cx + arcR * std::cos(aTo), cy + arcR * std::sin(aTo));
                arc.size          = D2D1::SizeF(arcR, arcR);
                arc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                arc.arcSize       = D2D1_ARC_SIZE_SMALL;
                sink->AddArc(arc);
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
                sink->Close();
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> wb;
                rt->CreateSolidColorBrush(Tok::White(0.08f * alpha), wb.GetAddressOf());
                if (wb) rt->DrawGeometry(arcGeom.Get(), wb.Get(), 1.0f * s);
            }
        }
    }

    if (m_items.empty()) return;

    // ---- Item circles + icons + labels -------------------------------------
    const float glowBreathe = 0.5f + 0.5f * std::sin(m_glowPhase);

    for (int32_t i = 0; i < static_cast<int32_t>(m_items.size()); ++i) {
        const float itemSc = (i < static_cast<int32_t>(m_itemScales.size()))
                             ? std::min(1.2f, m_itemScales[static_cast<size_t>(i)]) : sc;
        const Vec2   pos   = PositionForIndex(i, cx, cy, radius);
        const bool   hovered = (i == displayHovered);
        const float  px = pos.x, py = pos.y;
        const float  iR = itemR * itemSc;

        if (iR <= 0.0f) continue;

        // Animated outer glow ring for hovered item
        if (hovered) {
            const float glowR = iR * (1.55f + 0.12f * glowBreathe);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> g;
            rt->CreateSolidColorBrush(
                Tok::GoldGlow((0.22f + 0.12f * glowBreathe) * alpha), g.GetAddressOf());
            if (g) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px,py), glowR, glowR), g.Get());

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> g2;
            rt->CreateSolidColorBrush(Tok::GoldGlow(0.18f * alpha), g2.GetAddressOf());
            if (g2) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px,py), iR * 1.25f, iR * 1.25f), g2.Get());
        }

        // Circle fill (sunken deep)
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(Tok::SurfaceSunken(0.90f * alpha), b.GetAddressOf());
            if (b) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px,py), iR, iR), b.Get());
        }
        // Inner raised surface (creates depth — like a pressed button face)
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(
                hovered ? Tok::SurfaceRaised(0.80f * alpha) : Tok::SurfaceBase(0.68f * alpha),
                b.GetAddressOf());
            if (b) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px,py), iR * 0.72f, iR * 0.72f), b.Get());
        }

        // Circle border
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(
                hovered ? Tok::GoldHi(0.95f * alpha) : Tok::GoldMid(0.45f * alpha),
                b.GetAddressOf());
            if (b) rt->DrawEllipse(
                D2D1::Ellipse(D2D1::Point2F(px,py), iR, iR), b.Get(),
                hovered ? 1.8f * s : 0.9f * s);
        }
        // Hovered: inner machined bevel ring (inset 2px, GoldWarm 30%)
        if (hovered) {
            const float bevelR = iR - 2.0f * s;
            if (bevelR > 0.0f) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bv;
                rt->CreateSolidColorBrush(Tok::GoldWarm(0.30f * alpha), bv.GetAddressOf());
                if (bv) rt->DrawEllipse(
                    D2D1::Ellipse(D2D1::Point2F(px,py), bevelR, bevelR), bv.Get(), 0.8f * s);
            }
        }

        if (!dwrite) continue;

        // Icon
        const std::wstring& iconStr = m_items[static_cast<size_t>(i)].icon;
        if (!iconStr.empty()) {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
            dwrite->CreateTextFormat(
                L"Segoe UI Emoji", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                18.0f * itemSc * s, L"en-us", fmt.GetAddressOf());
            if (fmt) {
                fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tb;
                rt->CreateSolidColorBrush(
                    hovered ? Tok::GoldHi(alpha) : Tok::ChromeMid(0.88f * alpha),
                    tb.GetAddressOf());
                if (tb) rt->DrawText(iconStr.c_str(), static_cast<UINT32>(iconStr.size()),
                    fmt.Get(), D2D1::RectF(px-iR, py-iR, px+iR, py+iR), tb.Get());
            }
        }

        // Label
        const std::wstring& labelStr = m_items[static_cast<size_t>(i)].label;
        if (!labelStr.empty()) {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> lf;
            dwrite->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                11.0f * itemSc * s, L"en-us", lf.GetAddressOf());
            if (lf) {
                lf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                lf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> lb;
                rt->CreateSolidColorBrush(
                    hovered ? Tok::GoldHi(alpha) : Tok::ChromeMute(0.78f * alpha),
                    lb.GetAddressOf());
                if (lb) {
                    const float lw = 80.0f * itemSc * s;
                    rt->DrawText(labelStr.c_str(), static_cast<UINT32>(labelStr.size()),
                        lf.Get(),
                        D2D1::RectF(px - lw*0.5f, py + iR + 4.0f*itemSc*s,
                                    px + lw*0.5f, py + iR + 22.0f*itemSc*s),
                        lb.Get());
                }
            }
        }
    }

    // ---- Centre disc: active label + button hint ---------------------------
    if (dwrite && displayHovered >= 0) {
        const wchar_t* hint = m_items[static_cast<size_t>(displayHovered)].label.c_str();
        if (hint && hint[0] != L'\0') {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> hf;
            dwrite->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                13.5f * sc * s, L"en-us", hf.GetAddressOf());
            if (hf) {
                hf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                hf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hb;
                rt->CreateSolidColorBrush(Tok::GoldHi(alpha), hb.GetAddressOf());
                if (hb) rt->DrawText(hint, static_cast<UINT32>(std::wcslen(hint)),
                    hf.Get(),
                    D2D1::RectF(cx-discR, cy-discR*0.6f, cx+discR, cy+discR*0.2f),
                    hb.Get());
            }
        }
        const wchar_t* hintLine = L"\u25CF Confirm  \u25C6 Cancel";
        Microsoft::WRL::ComPtr<IDWriteTextFormat> sf;
        dwrite->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            9.0f * sc * s, L"en-us", sf.GetAddressOf());
        if (sf) {
            sf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            sf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sb;
            rt->CreateSolidColorBrush(Tok::ChromeMute(0.50f * alpha), sb.GetAddressOf());
            if (sb) rt->DrawText(hintLine, static_cast<UINT32>(std::wcslen(hintLine)),
                sf.Get(),
                D2D1::RectF(cx-discR, cy+discR*0.22f, cx+discR, cy+discR*0.80f),
                sb.Get());
        }
    }
}

} // namespace enjoystick::overlay
