#include <enjoystick/overlay/RadialMenu.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include <cmath>
#include <algorithm>
#include <cassert>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace enjoystick::overlay {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static constexpr float kTwoPi = static_cast<float>(M_PI * 2.0);

static inline float Lerp(float a, float b, float t) noexcept {
    return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

// Smooth-step easing for animation (no library dependency)
static inline float SmoothStep(float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static inline D2D1_COLOR_F UnpackColor(uint32_t abgr) noexcept {
    return D2D1_COLOR_F {
        /* r */ ((abgr >>  0) & 0xFF) / 255.0f,
        /* g */ ((abgr >>  8) & 0xFF) / 255.0f,
        /* b */ ((abgr >> 16) & 0xFF) / 255.0f,
        /* a */ ((abgr >> 24) & 0xFF) / 255.0f,
    };
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

RadialMenu::RadialMenu(Config config)
    : m_config(std::move(config)) {}

RadialMenu::~RadialMenu() = default;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void RadialMenu::SetItems(std::vector<RadialMenuItem> items) {
    m_items        = std::move(items);
    m_hoveredIndex = -1;
}

void RadialMenu::Open() {
    if (m_state == State::Hidden || m_state == State::Closing) {
        m_state        = State::Opening;
        m_hoveredIndex = -1;
    }
}

void RadialMenu::Close() {
    if (m_state == State::Visible || m_state == State::Opening) {
        m_state = State::Closing;
    }
}

bool RadialMenu::IsVisible() const noexcept {
    return m_state != State::Hidden;
}

RadialMenu::State RadialMenu::GetState() const noexcept {
    return m_state;
}

int32_t RadialMenu::GetHoveredIndex() const noexcept {
    return m_hoveredIndex;
}

// ---------------------------------------------------------------------------
// Update — called every frame from the overlay thread
// ---------------------------------------------------------------------------

void RadialMenu::Update(const ControllerState& state, float deltaSeconds) {
    UpdateAnimation(deltaSeconds);

    if (m_state != State::Visible && m_state != State::Opening) return;

    UpdateSelection(state.rightStick);

    // South (A/Cross) — confirm
    const bool south = HasButton(state.buttons, Button::South);
    if (south && !m_prevSouth) ConfirmSelection();
    m_prevSouth = south;

    // East (B/Circle) — cancel
    const bool east = HasButton(state.buttons, Button::East);
    if (east && !m_prevEast) Close();
    m_prevEast = east;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void RadialMenu::UpdateAnimation(float deltaSeconds) {
    const float step = deltaSeconds / (m_config.animMs * 0.001f);
    switch (m_state) {
        case State::Opening:
            m_animProgress += step;
            if (m_animProgress >= 1.0f) {
                m_animProgress = 1.0f;
                m_state        = State::Visible;
            }
            break;
        case State::Closing:
            m_animProgress -= step;
            if (m_animProgress <= 0.0f) {
                m_animProgress = 0.0f;
                m_state        = State::Hidden;
            }
            break;
        default:
            break;
    }
}

void RadialMenu::UpdateSelection(Vec2 stick) {
    const float mag = std::sqrt(stick.x * stick.x + stick.y * stick.y);
    if (mag < m_config.selectionDeadzone || m_items.empty()) {
        m_hoveredIndex = -1;
        return;
    }
    // atan2: 0 = right, positive = down (screen coords)
    const float angle = std::atan2(stick.y, stick.x);
    // Normalise to [0, 2pi)
    float norm = angle < 0.0f ? angle + kTwoPi : angle;
    // Sector width
    const float sector = kTwoPi / static_cast<float>(m_items.size());
    // Offset by half a sector so the first item is at the top (angle = -pi/2)
    norm = std::fmod(norm + sector * 0.5f + kTwoPi, kTwoPi);
    m_hoveredIndex = static_cast<int32_t>(norm / sector);
    m_hoveredIndex = std::clamp(m_hoveredIndex, 0,
                                 static_cast<int32_t>(m_items.size()) - 1);
}

void RadialMenu::ConfirmSelection() {
    if (m_hoveredIndex < 0 ||
        m_hoveredIndex >= static_cast<int32_t>(m_items.size())) return;

    const auto& item = m_items[static_cast<size_t>(m_hoveredIndex)];
    if (item.action) item.action();
    Close();
}

float RadialMenu::AngleForIndex(int32_t index) const noexcept {
    const float sector = kTwoPi / static_cast<float>(m_items.size());
    // Start from top (-pi/2), go clockwise
    return -static_cast<float>(M_PI) * 0.5f + static_cast<float>(index) * sector;
}

Vec2 RadialMenu::PositionForIndex(int32_t index, float scale) const noexcept {
    const float angle = AngleForIndex(index);
    const float r     = m_config.radius * scale;
    return { r * std::cos(angle), r * std::sin(angle) };
}

// ---------------------------------------------------------------------------
// Draw — Direct2D rendering
// ---------------------------------------------------------------------------

void RadialMenu::Draw(void* d2dRenderTarget, float dpiScale) const {
    if (m_state == State::Hidden || m_items.empty()) return;

    auto* rt = static_cast<ID2D1RenderTarget*>(d2dRenderTarget);
    const float eased = SmoothStep(m_animProgress);

    // Determine centre
    D2D1_SIZE_F sz = rt->GetSize();
    const float cx = (m_config.centreX < 0) ? sz.width  * 0.5f
                                              : static_cast<float>(m_config.centreX);
    const float cy = (m_config.centreY < 0) ? sz.height * 0.5f
                                              : static_cast<float>(m_config.centreY);

    // Background disc
    ID2D1SolidColorBrush* bgBrush = nullptr;
    rt->CreateSolidColorBrush(UnpackColor(m_config.colourBackground), &bgBrush);
    if (bgBrush) {
        bgBrush->SetOpacity(eased);
        rt->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(cx, cy),
                          m_config.radius * eased * dpiScale,
                          m_config.radius * eased * dpiScale),
            bgBrush);
        bgBrush->Release();
    }

    const int32_t count = static_cast<int32_t>(m_items.size());
    for (int32_t i = 0; i < count; ++i) {
        const Vec2  pos  = PositionForIndex(i, eased);
        const float ix   = cx + pos.x * dpiScale;
        const float iy   = cy + pos.y * dpiScale;
        const bool  hov  = (i == m_hoveredIndex);

        const uint32_t colPacked = hov ? m_config.colourItemHovered
                                       : m_config.colourItemNormal;
        const float itemR = (hov ? 44.0f : 36.0f) * eased * dpiScale;

        ID2D1SolidColorBrush* itemBrush = nullptr;
        rt->CreateSolidColorBrush(UnpackColor(colPacked), &itemBrush);
        if (itemBrush) {
            itemBrush->SetOpacity(eased);
            rt->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(ix, iy), itemR, itemR),
                itemBrush);
            itemBrush->Release();
        }
    }
}

} // namespace enjoystick::overlay
