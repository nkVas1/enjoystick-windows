// WIN32_LEAN_AND_MEAN and NOMINMAX are injected by CMake for this module.
#include <enjoystick/overlay/RadialMenu.hpp>

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
// Construction
// ---------------------------------------------------------------------------

RadialMenu::RadialMenu(Config config)
    : m_config(std::move(config))
{}

RadialMenu::~RadialMenu() = default;

// ---------------------------------------------------------------------------
// Item management
// ---------------------------------------------------------------------------

void RadialMenu::SetItems(std::vector<RadialMenuItem> items) {
    m_items        = std::move(items);
    m_hoveredIndex = -1;
}

bool              RadialMenu::IsVisible()      const noexcept { return m_state != State::Hidden; }
RadialMenu::State RadialMenu::GetState()        const noexcept { return m_state; }
int32_t           RadialMenu::GetHoveredIndex() const noexcept { return m_hoveredIndex; }

// ---------------------------------------------------------------------------
// Open / Close
// ---------------------------------------------------------------------------

void RadialMenu::Open() {
    if (m_state == State::Visible || m_state == State::Opening) return;
    m_state          = State::Opening;
    m_hoveredIndex   = -1;
    m_confirmedIndex = -1;
}

void RadialMenu::Close() {
    if (m_state == State::Hidden || m_state == State::Closing) return;
    m_state = State::Closing;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void RadialMenu::Update(const ControllerState& state, float deltaSeconds) {
    UpdateAnimation(deltaSeconds);

    if (m_state == State::Hidden) {
        m_prevSouth = m_prevEast = false;
        return;
    }

    const bool south = HasButton(state.buttons, Button::South);
    const bool east  = HasButton(state.buttons, Button::East);

    UpdateSelection(state.rightStick);

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
    default:
        break;
    }
}

void RadialMenu::UpdateSelection(Vec2 stick) {
    if (m_items.empty()) return;

    if (stick.LengthSq() < m_config.selectionDeadzone * m_config.selectionDeadzone) {
        m_hoveredIndex = -1;
        return;
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

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

float RadialMenu::AngleForIndex(int32_t index) const noexcept {
    const float sector = k2Pi / static_cast<float>(m_items.size());
    return -kPi * 0.5f + static_cast<float>(index) * sector;
}

Vec2 RadialMenu::PositionForIndex(int32_t index, float cx, float cy, float radius) const noexcept {
    const float a = AngleForIndex(index);
    return { cx + radius * std::cos(a), cy + radius * std::sin(a) };
}

// ---------------------------------------------------------------------------
// Draw
//
// Signature extended with IDWriteFactory* and screen dimensions so the menu
// can centre itself and render text without external state.
// ---------------------------------------------------------------------------

void RadialMenu::Draw(
    void*           renderTargetPtr,
    void*           dwriteFactoryPtr,
    float           dpiScale,
    float           screenW,
    float           screenH) const
{
    if (m_state == State::Hidden || m_animProgress <= 0.0f) return;
    if (!renderTargetPtr) return;

    auto* rt    = static_cast<ID2D1RenderTarget*>(renderTargetPtr);
    auto* dwrite = static_cast<IDWriteFactory*>(dwriteFactoryPtr);

    const float scale  = m_animProgress;   // 0→1 scale-in
    const float alpha  = m_animProgress;   // fade-in tied to scale

    // Centre position
    const float cx = (m_config.centreX < 0) ? screenW * 0.5f
                                             : static_cast<float>(m_config.centreX);
    const float cy = (m_config.centreY < 0) ? screenH * 0.5f
                                             : static_cast<float>(m_config.centreY);

    const float radius  = m_config.radius * scale * dpiScale;
    const float discR   = radius * 0.40f;  // centre disc radius
    const float itemR   = 32.0f * scale * dpiScale;

    // Helper: ABGR uint32 → D2D1_COLOR_F with alpha multiplier
    auto col = [alpha](uint32_t c, float extra = 1.0f) -> D2D1_COLOR_F {
        return {
            ((c >>  0) & 0xFF) / 255.0f,
            ((c >>  8) & 0xFF) / 255.0f,
            ((c >> 16) & 0xFF) / 255.0f,
            ((c >> 24) & 0xFF) / 255.0f * alpha * extra
        };
    };

    // --- Sector highlight for hovered item -----------------------------------
    if (m_hoveredIndex >= 0 && m_items.size() > 1) {
        const int32_t n = static_cast<int32_t>(m_items.size());
        const float sector = k2Pi / static_cast<float>(n);
        const float startA = AngleForIndex(m_hoveredIndex) - sector * 0.5f;
        const float endA   = startA + sector;
        const float outerR = radius + itemR * 0.6f;

        Microsoft::WRL::ComPtr<ID2D1PathGeometry> geom;
        if (rt->GetFactory) {
            // Retrieve factory from the render target to create geometry
            Microsoft::WRL::ComPtr<ID2D1Factory> factory;
            rt->GetFactory(factory.GetAddressOf());
            if (factory) {
                factory->CreatePathGeometry(geom.GetAddressOf());
            }
        }
        if (geom) {
            Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
            geom->Open(sink.GetAddressOf());
            if (sink) {
                const float cosS = std::cos(startA), sinS = std::sin(startA);
                const float cosE = std::cos(endA),   sinE = std::sin(endA);
                sink->BeginFigure(
                    D2D1::Point2F(cx + discR * cosS, cy + discR * sinS),
                    D2D1_FIGURE_BEGIN_FILLED);
                // Inner arc
                D2D1_ARC_SEGMENT innerArc{};
                innerArc.point        = D2D1::Point2F(cx + discR * cosE, cy + discR * sinE);
                innerArc.size         = D2D1::SizeF(discR, discR);
                innerArc.rotationAngle = 0.0f;
                innerArc.sweepDirection = D2D1_SWEEP_DIRECTION_CLOCKWISE;
                innerArc.arcSize      = (sector > kPi) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
                sink->AddArc(innerArc);
                // Line to outer
                sink->AddLine(D2D1::Point2F(cx + outerR * cosE, cy + outerR * sinE));
                // Outer arc (reverse)
                D2D1_ARC_SEGMENT outerArc{};
                outerArc.point        = D2D1::Point2F(cx + outerR * cosS, cy + outerR * sinS);
                outerArc.size         = D2D1::SizeF(outerR, outerR);
                outerArc.rotationAngle = 0.0f;
                outerArc.sweepDirection = D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE;
                outerArc.arcSize      = (sector > kPi) ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL;
                sink->AddArc(outerArc);
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();

                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sBrush;
                rt->CreateSolidColorBrush(
                    D2D1::ColorF(0.35f, 0.31f, 0.95f, 0.25f * alpha),
                    sBrush.GetAddressOf());
                if (sBrush) rt->FillGeometry(geom.Get(), sBrush.Get());
            }
        }
    }

    // --- Background disc -----------------------------------------------------
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bg;
        rt->CreateSolidColorBrush(col(m_config.colourBackground), bg.GetAddressOf());
        if (bg) rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), discR, discR), bg.Get());
    }

    if (m_items.empty()) return;

    // --- Item circles + text -------------------------------------------------
    for (int32_t i = 0; i < static_cast<int32_t>(m_items.size()); ++i) {
        const Vec2 pos    = PositionForIndex(i, cx, cy, radius);
        const bool hovered = (i == m_hoveredIndex);

        // Circle background
        const uint32_t circlCol = hovered
            ? m_config.colourItemHovered
            : m_config.colourItemNormal;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cBrush;
        rt->CreateSolidColorBrush(col(circlCol), cBrush.GetAddressOf());
        if (cBrush)
            rt->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(pos.x, pos.y), itemR, itemR),
                cBrush.Get());

        if (!dwrite) continue;

        // Icon (emoji / unicode symbol) — large, centred
        const std::wstring& iconStr = m_items[static_cast<size_t>(i)].icon;
        if (!iconStr.empty()) {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> iconFmt;
            dwrite->CreateTextFormat(
                L"Segoe UI Emoji", nullptr,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                18.0f * scale * dpiScale, L"en-us",
                iconFmt.GetAddressOf());
            if (iconFmt) {
                iconFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                iconFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush;
                rt->CreateSolidColorBrush(col(m_config.colourText), textBrush.GetAddressOf());
                if (textBrush) {
                    const float half = itemR;
                    rt->DrawText(
                        iconStr.c_str(),
                        static_cast<UINT32>(iconStr.size()),
                        iconFmt.Get(),
                        D2D1::RectF(pos.x - half, pos.y - half, pos.x + half, pos.y + half),
                        textBrush.Get());
                }
            }
        }

        // Label — small text below the item circle
        const std::wstring& labelStr = m_items[static_cast<size_t>(i)].label;
        if (!labelStr.empty()) {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> labelFmt;
            dwrite->CreateTextFormat(
                L"Segoe UI", nullptr,
                DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                11.0f * scale * dpiScale, L"en-us",
                labelFmt.GetAddressOf());
            if (labelFmt) {
                labelFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                labelFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> lBrush;
                rt->CreateSolidColorBrush(
                    col(m_config.colourText, hovered ? 1.0f : 0.75f),
                    lBrush.GetAddressOf());
                if (lBrush) {
                    const float lw = 80.0f * scale * dpiScale;
                    rt->DrawText(
                        labelStr.c_str(),
                        static_cast<UINT32>(labelStr.size()),
                        labelFmt.Get(),
                        D2D1::RectF(pos.x - lw * 0.5f,
                                    pos.y + itemR + 4.0f * scale * dpiScale,
                                    pos.x + lw * 0.5f,
                                    pos.y + itemR + 22.0f * scale * dpiScale),
                        lBrush.Get());
                }
            }
        }
    }

    // --- Hint text in centre disc -------------------------------------------
    if (dwrite) {
        const wchar_t* hint = (m_hoveredIndex >= 0)
            ? m_items[static_cast<size_t>(m_hoveredIndex)].label.c_str()
            : L"";
        if (hint && hint[0] != L'\0') {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> hintFmt;
            dwrite->CreateTextFormat(
                L"Segoe UI", nullptr,
                DWRITE_FONT_WEIGHT_BOLD,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                13.0f * scale * dpiScale, L"en-us",
                hintFmt.GetAddressOf());
            if (hintFmt) {
                hintFmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                hintFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hBrush;
                rt->CreateSolidColorBrush(
                    D2D1::ColorF(0.96f, 0.96f, 1.0f, alpha),
                    hBrush.GetAddressOf());
                if (hBrush) {
                    rt->DrawText(
                        hint,
                        static_cast<UINT32>(std::wcslen(hint)),
                        hintFmt.Get(),
                        D2D1::RectF(cx - discR, cy - discR, cx + discR, cy + discR),
                        hBrush.Get());
                }
            }
        }
    }
}

} // namespace enjoystick::overlay
