// NOTE: WIN32_LEAN_AND_MEAN and NOMINMAX are injected by CMake for this module.
#include <enjoystick/overlay/RadialMenu.hpp>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

#include <cmath>
#include <algorithm>
#include <stdexcept>

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

bool  RadialMenu::IsVisible()    const noexcept { return m_state != State::Hidden; }
State RadialMenu::GetState()     const noexcept { return m_state; }
int32_t RadialMenu::GetHoveredIndex() const noexcept { return m_hoveredIndex; }

// ---------------------------------------------------------------------------
// Open / Close
// ---------------------------------------------------------------------------

void RadialMenu::Open() {
    if (m_state == State::Visible || m_state == State::Opening) return;
    m_state        = State::Opening;
    m_hoveredIndex = -1;
    m_confirmedIndex = -1;
}

void RadialMenu::Close() {
    if (m_state == State::Hidden || m_state == State::Closing) return;
    m_state = State::Closing;
}

// ---------------------------------------------------------------------------
// Update — called every render frame
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

    // Confirm on South rising edge
    if (south && !m_prevSouth) ConfirmSelection();

    // Cancel on East rising edge
    if (east && !m_prevEast) Close();

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

    // atan2 returns angle in [-pi, pi]; convert to [0, 2pi]
    float angle = std::atan2(stick.y, stick.x);
    if (angle < 0.0f) angle += static_cast<float>(2.0 * M_PI);

    // Sector width
    const float sectorWidth = static_cast<float>(2.0 * M_PI) /
                              static_cast<float>(m_items.size());
    // Offset so item 0 is at top (12 o’clock = -pi/2)
    const float adjusted = std::fmod(
        angle + static_cast<float>(M_PI / 2.0), static_cast<float>(2.0 * M_PI));
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
    // Item 0 at top (-pi/2), clockwise
    const float sectorWidth = static_cast<float>(2.0 * M_PI) /
                              static_cast<float>(m_items.size());
    return -static_cast<float>(M_PI / 2.0) +
           static_cast<float>(index) * sectorWidth;
}

Vec2 RadialMenu::PositionForIndex(int32_t index, float scale) const noexcept {
    const float angle = AngleForIndex(index);
    const float r     = m_config.radius * scale;
    float cx = (m_config.centreX < 0) ? 960.0f : static_cast<float>(m_config.centreX);
    float cy = (m_config.centreY < 0) ? 540.0f : static_cast<float>(m_config.centreY);
    return { cx + r * std::cos(angle), cy + r * std::sin(angle) };
}

// ---------------------------------------------------------------------------
// Draw  — accepts void* cast back to ID2D1RenderTarget*
// ---------------------------------------------------------------------------

void RadialMenu::Draw(void* renderTargetPtr, float dpiScale) const {
    if (m_state == State::Hidden || m_animProgress <= 0.0f) return;
    if (!renderTargetPtr) return;

    // The render target pointer is passed as void* to avoid pulling D2D headers
    // into the public header. Cast back to ID2D1RenderTarget*.
    auto* rt = static_cast<ID2D1RenderTarget*>(renderTargetPtr);

    const float scale  = m_animProgress;  // [0,1] drives both size and opacity
    const float alpha  = scale;

    // ----- Background disc -----------------------------------------------
    auto abgr = [](uint32_t c, float a) -> D2D1_COLOR_F {
        // Input is ABGR layout stored as uint32 (see RadialMenu.hpp)
        return {
            ((c >>  0) & 0xFF) / 255.0f,   // R
            ((c >>  8) & 0xFF) / 255.0f,   // G
            ((c >> 16) & 0xFF) / 255.0f,   // B
            ((c >> 24) & 0xFF) / 255.0f * a // A
        };
    };

    const float cx = (m_config.centreX < 0) ? 960.0f : static_cast<float>(m_config.centreX);
    const float cy = (m_config.centreY < 0) ? 540.0f : static_cast<float>(m_config.centreY);
    const float discR = m_config.radius * 0.45f * scale * dpiScale;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bgBrush;
    rt->CreateSolidColorBrush(abgr(m_config.colourBackground, alpha), bgBrush.GetAddressOf());
    if (bgBrush)
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), discR, discR), bgBrush.Get());

    if (m_items.empty()) return;

    // ----- Item slices ---------------------------------------------------
    for (int32_t i = 0; i < static_cast<int32_t>(m_items.size()); ++i) {
        const Vec2 pos = PositionForIndex(i, scale * dpiScale);
        const bool hovered = (i == m_hoveredIndex);

        const uint32_t colItem = hovered ? m_config.colourItemHovered
                                         : m_config.colourItemNormal;
        const float itemR = 30.0f * scale * dpiScale;

        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> itemBrush;
        rt->CreateSolidColorBrush(abgr(colItem, alpha), itemBrush.GetAddressOf());
        if (itemBrush)
            rt->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(pos.x, pos.y), itemR, itemR),
                itemBrush.Get());
    }
}

} // namespace enjoystick::overlay
