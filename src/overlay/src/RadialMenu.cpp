// NOTE: WIN32_LEAN_AND_MEAN and NOMINMAX are injected by CMake for this module.
#include <enjoystick/overlay/RadialMenu.hpp>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

#include <cmath>
#include <algorithm>

#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif

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

bool             RadialMenu::IsVisible()      const noexcept { return m_state != State::Hidden; }
RadialMenu::State RadialMenu::GetState()       const noexcept { return m_state; }
int32_t          RadialMenu::GetHoveredIndex() const noexcept { return m_hoveredIndex; }

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
        m_prevSouth = m_prevEast = m_prevGuide = false;
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

    const float magSq = stick.LengthSq();
    if (magSq < m_config.selectionDeadzone * m_config.selectionDeadzone) {
        m_hoveredIndex = -1;
        return;
    }

    float angle = std::atan2(stick.y, stick.x);
    if (angle < 0.0f) angle += static_cast<float>(2.0 * M_PI);

    const float sectorWidth = static_cast<float>(2.0 * M_PI) /
                              static_cast<float>(m_items.size());
    // Offset so item 0 is at top (12 o'clock)
    const float adjusted = std::fmod(
        angle + static_cast<float>(M_PI / 2.0),
        static_cast<float>(2.0 * M_PI));
    m_hoveredIndex = static_cast<int32_t>(adjusted / sectorWidth) %
                     static_cast<int32_t>(m_items.size());
}

void RadialMenu::ConfirmSelection() {
    if (m_hoveredIndex < 0 ||
        m_hoveredIndex >= static_cast<int32_t>(m_items.size())) return;
    m_confirmedIndex = m_hoveredIndex;
    const auto& action = m_items[static_cast<size_t>(m_confirmedIndex)].action;
    if (action) action();
    Close();
}

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------

float RadialMenu::AngleForIndex(int32_t index) const noexcept {
    const float sectorWidth = static_cast<float>(2.0 * M_PI) /
                              static_cast<float>(m_items.size());
    return -static_cast<float>(M_PI / 2.0) +
           static_cast<float>(index) * sectorWidth;
}

Vec2 RadialMenu::PositionForIndex(int32_t index, float scale) const noexcept {
    const float angle = AngleForIndex(index);
    const float r     = m_config.radius * scale;
    const float cx    = (m_config.centreX < 0) ? 960.0f : static_cast<float>(m_config.centreX);
    const float cy    = (m_config.centreY < 0) ? 540.0f : static_cast<float>(m_config.centreY);
    return { cx + r * std::cos(angle), cy + r * std::sin(angle) };
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void RadialMenu::Draw(void* renderTargetPtr, float dpiScale) const {
    if (m_state == State::Hidden || m_animProgress <= 0.0f) return;
    if (!renderTargetPtr) return;

    auto* rt = static_cast<ID2D1RenderTarget*>(renderTargetPtr);

    const float scale = m_animProgress;
    const float alpha = scale;

    // Unpack ABGR uint32 -> D2D1_COLOR_F
    auto toColor = [](uint32_t c, float a) -> D2D1_COLOR_F {
        return {
            ((c >>  0) & 0xFF) / 255.0f,
            ((c >>  8) & 0xFF) / 255.0f,
            ((c >> 16) & 0xFF) / 255.0f,
            ((c >> 24) & 0xFF) / 255.0f * a
        };
    };

    const float cx    = (m_config.centreX < 0) ? 960.0f : static_cast<float>(m_config.centreX);
    const float cy    = (m_config.centreY < 0) ? 540.0f : static_cast<float>(m_config.centreY);
    const float discR = m_config.radius * 0.45f * scale * dpiScale;

    // Background disc
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    rt->CreateSolidColorBrush(toColor(m_config.colourBackground, alpha), bgBrush.GetAddressOf());
    if (bgBrush)
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), discR, discR), bgBrush.Get());

    if (m_items.empty()) return;

    // Item circles
    for (int32_t i = 0; i < static_cast<int32_t>(m_items.size()); ++i) {
        const Vec2  pos     = PositionForIndex(i, scale * dpiScale);
        const bool  hovered = (i == m_hoveredIndex);
        const float itemR   = 30.0f * scale * dpiScale;

        const uint32_t col = hovered ? m_config.colourItemHovered : m_config.colourItemNormal;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
        rt->CreateSolidColorBrush(toColor(col, alpha), brush.GetAddressOf());
        if (brush)
            rt->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(pos.x, pos.y), itemR, itemR),
                brush.Get());
    }
}

} // namespace enjoystick::overlay
