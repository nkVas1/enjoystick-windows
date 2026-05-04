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

RadialMenu::RadialMenu(Config config) : m_config(std::move(config)) {}
RadialMenu::~RadialMenu() = default;

void RadialMenu::SetItems(std::vector<RadialMenuItem> items) {
    m_items        = std::move(items);
    m_hoveredIndex = -1;
}

bool              RadialMenu::IsVisible()      const noexcept { return m_state != State::Hidden; }
RadialMenu::State RadialMenu::GetState()        const noexcept { return m_state; }
int32_t           RadialMenu::GetHoveredIndex() const noexcept { return m_hoveredIndex; }

void RadialMenu::Open() {
    if (m_state == State::Visible || m_state == State::Opening) return;
    m_state = State::Opening;
    m_hoveredIndex = m_confirmedIndex = -1;
}
void RadialMenu::Close() {
    if (m_state == State::Hidden || m_state == State::Closing) return;
    m_state = State::Closing;
}

void RadialMenu::Update(const ControllerState& state, float deltaSeconds) {
    UpdateAnimation(deltaSeconds);
    if (m_state == State::Hidden) { m_prevSouth = m_prevEast = false; return; }
    UpdateSelection(state.rightStick);
    const bool south = HasButton(state.buttons, Button::South);
    const bool east  = HasButton(state.buttons, Button::East);
    if (south && !m_prevSouth) ConfirmSelection();
    if (east  && !m_prevEast)  Close();
    m_prevSouth = south;
    m_prevEast  = east;
}

void RadialMenu::UpdateAnimation(float deltaSeconds) {
    const float step = (deltaSeconds * 1000.0f) / m_config.animMs;
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

void RadialMenu::UpdateSelection(Vec2 stick) {
    if (m_items.empty()) return;
    if (stick.LengthSq() < m_config.selectionDeadzone * m_config.selectionDeadzone) {
        m_hoveredIndex = -1; return;
    }
    float angle = std::atan2(stick.y, stick.x);
    if (angle < 0.0f) angle += k2Pi;
    const float sector   = k2Pi / static_cast<float>(m_items.size());
    const float adjusted = std::fmod(angle + kPi * 0.5f, k2Pi);
    m_hoveredIndex = static_cast<int32_t>(adjusted / sector) %
                     static_cast<int32_t>(m_items.size());
}

void RadialMenu::ConfirmSelection() {
    if (m_hoveredIndex < 0 ||
        m_hoveredIndex >= static_cast<int32_t>(m_items.size())) return;
    m_confirmedIndex = m_hoveredIndex;
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
// Draw  — Dark Regalia visual language
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

    const float sc    = m_animProgress;
    const float alpha = m_animProgress;

    const float cx = (m_config.centreX < 0) ? screenW * 0.5f : static_cast<float>(m_config.centreX);
    const float cy = (m_config.centreY < 0) ? screenH * 0.5f : static_cast<float>(m_config.centreY);
    const float radius = m_config.radius * sc * dpiScale;
    const float discR  = radius * 0.40f;
    const float itemR  = 32.0f * sc * dpiScale;
    const float s      = dpiScale;

    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    rt->GetFactory(factory.GetAddressOf());

    // ---- Outer ornamental gold ring ----------------------------------------
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldShadow(0.30f * alpha), b.GetAddressOf());
        if (b) rt->DrawEllipse(
                   D2D1::Ellipse(D2D1::Point2F(cx,cy), radius + itemR*0.8f, radius + itemR*0.8f),
                   b.Get(), 1.0f * s);
    }
    // Thin secondary ornament ring just inside outer
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldGlow(0.18f * alpha), b.GetAddressOf());
        if (b) rt->DrawEllipse(
                   D2D1::Ellipse(D2D1::Point2F(cx,cy), radius + itemR*0.5f, radius + itemR*0.5f),
                   b.Get(), 0.6f * s);
    }

    // ---- Sector highlight ---------------------------------------------------
    if (m_hoveredIndex >= 0 && m_items.size() > 1 && factory) {
        const int32_t n      = static_cast<int32_t>(m_items.size());
        const float   sector = k2Pi / static_cast<float>(n);
        const float   startA = AngleForIndex(m_hoveredIndex) - sector * 0.5f;
        const float   endA   = startA + sector;
        const float   outerR = radius + itemR * 0.6f;

        Microsoft::WRL::ComPtr<ID2D1PathGeometry> geom;
        factory->CreatePathGeometry(geom.GetAddressOf());
        if (geom) {
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
            geom->Open(sink.GetAddressOf());
            if (sink) {
                const float cosS = std::cos(startA), sinS = std::sin(startA);
                const float cosE = std::cos(endA),   sinE = std::sin(endA);
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

                // Dark void sector fill
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fill;
                rt->CreateSolidColorBrush(Tok::DeepVoid(0.72f * alpha), fill.GetAddressOf());
                if (fill) rt->FillGeometry(geom.Get(), fill.Get());

                // Gold arc border on the outer edge
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
                        rt->CreateSolidColorBrush(Tok::GoldMid(0.80f * alpha), gb.GetAddressOf());
                        if (gb) rt->DrawGeometry(borderGeom.Get(), gb.Get(), 1.5f * s);
                    }
                }
            }
        }
    }

    // ---- Background disc ---------------------------------------------------
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bg;
        rt->CreateSolidColorBrush(Tok::DeepVoid(0.82f * alpha), bg.GetAddressOf());
        if (bg) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy), discR, discR), bg.Get());
    }
    // Gold ring around centre disc
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::GoldMid(0.55f * alpha), b.GetAddressOf());
        if (b) rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx,cy), discR, discR),
                               b.Get(), 1.2f * s);
    }

    if (m_items.empty()) return;

    // ---- Item circles + icons + labels -------------------------------------
    for (int32_t i = 0; i < static_cast<int32_t>(m_items.size()); ++i) {
        const Vec2  pos     = PositionForIndex(i, cx, cy, radius);
        const bool  hovered = (i == m_hoveredIndex);
        const float px = pos.x, py = pos.y;

        // Glow under hovered item
        if (hovered) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> g;
            rt->CreateSolidColorBrush(Tok::GoldGlow(0.30f * alpha), g.GetAddressOf());
            if (g) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px,py), itemR*1.6f, itemR*1.6f), g.Get());
        }
        // Circle fill
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(Tok::ObsidianBase(0.88f * alpha), b.GetAddressOf());
            if (b) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(px,py), itemR, itemR), b.Get());
        }
        // Circle border: gold mid normally, gold hi when hovered
        {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            const auto col = hovered ? Tok::GoldHi(0.95f * alpha) : Tok::GoldMid(0.55f * alpha);
            rt->CreateSolidColorBrush(col, b.GetAddressOf());
            if (b) rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(px,py), itemR, itemR), b.Get(),
                                   hovered ? 1.5f * s : 0.8f * s);
        }

        if (!dwrite) continue;

        // Icon
        const std::wstring& iconStr = m_items[static_cast<size_t>(i)].icon;
        if (!iconStr.empty()) {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
            dwrite->CreateTextFormat(
                L"Segoe UI Emoji", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                18.0f * sc * s, L"en-us", fmt.GetAddressOf());
            if (fmt) {
                fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> tb;
                const auto tc = hovered ? Tok::GoldHi(alpha) : Tok::SilverMid(0.85f * alpha);
                rt->CreateSolidColorBrush(tc, tb.GetAddressOf());
                if (tb)
                    rt->DrawText(iconStr.c_str(), static_cast<UINT32>(iconStr.size()),
                                 fmt.Get(),
                                 D2D1::RectF(px-itemR, py-itemR, px+itemR, py+itemR),
                                 tb.Get());
            }
        }

        // Label
        const std::wstring& labelStr = m_items[static_cast<size_t>(i)].label;
        if (!labelStr.empty()) {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> lf;
            dwrite->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                11.0f * sc * s, L"en-us", lf.GetAddressOf());
            if (lf) {
                lf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                lf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> lb;
                const auto lc = hovered ? Tok::GoldHi(alpha) : Tok::SilverMute(0.80f * alpha);
                rt->CreateSolidColorBrush(lc, lb.GetAddressOf());
                if (lb) {
                    const float lw = 80.0f * sc * s;
                    rt->DrawText(labelStr.c_str(), static_cast<UINT32>(labelStr.size()),
                                 lf.Get(),
                                 D2D1::RectF(px - lw*0.5f, py + itemR + 4.0f*sc*s,
                                             px + lw*0.5f, py + itemR + 22.0f*sc*s),
                                 lb.Get());
                }
            }
        }
    }

    // ---- Centre hint -------------------------------------------------------
    if (dwrite && m_hoveredIndex >= 0) {
        const wchar_t* hint = m_items[static_cast<size_t>(m_hoveredIndex)].label.c_str();
        if (hint && hint[0] != L'\0') {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> hf;
            dwrite->CreateTextFormat(
                L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                13.0f * sc * s, L"en-us", hf.GetAddressOf());
            if (hf) {
                hf->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                hf->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hb;
                rt->CreateSolidColorBrush(Tok::GoldHi(alpha), hb.GetAddressOf());
                if (hb)
                    rt->DrawText(hint, static_cast<UINT32>(std::wcslen(hint)),
                                 hf.Get(),
                                 D2D1::RectF(cx-discR, cy-discR, cx+discR, cy+discR),
                                 hb.Get());
            }
        }
    }
}

} // namespace enjoystick::overlay
