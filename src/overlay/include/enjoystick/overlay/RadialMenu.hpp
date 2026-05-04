#pragma once

#include <enjoystick/shared/Types.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace enjoystick::overlay {

/// One entry in the radial menu.
struct RadialMenuItem {
    std::wstring          label;
    std::wstring          icon;    ///< Single emoji or unicode glyph
    std::function<void()> action;
};

///
/// RadialMenu — Dark Regalia premium radial selector.
///
/// - Driven by right-stick (or left, if configured).
/// - South (A/\u2715) confirms; East (B/\u25cb) cancels.
/// - Stick-latch: sector stays highlighted for `latchMs` after stick
///   returns to dead-zone, giving the user time to press Confirm.
/// - Bounce-open animation, fast-shrink close, staggered item pop-in.
/// - Full-screen dark scrim animates with the menu (blur proxy).
///
class RadialMenu {
public:
    struct Config {
        float openAnimMs        = 90.0f;
        float closeAnimMs       = 55.0f;
        float selectionDeadzone = 0.25f;
        float latchMs           = 350.0f;
        float radius            = 180.0f; ///< dp, scaled by dpiScale at draw time
        int   centreX           = -1;     ///< -1 = screen centre
        int   centreY           = -1;
    };

    enum class State : uint8_t { Hidden, Opening, Visible, Closing };

    explicit RadialMenu(Config config = {});
    ~RadialMenu();

    void SetItems(std::vector<RadialMenuItem> items);

    void Open();
    void Close();

    void Update(const ControllerState& state, float deltaSeconds);

    void Draw(void* renderTargetPtr, void* dwriteFactoryPtr,
              float dpiScale, float screenW, float screenH) const;

    [[nodiscard]] bool    IsVisible()      const noexcept;
    [[nodiscard]] State   GetState()        const noexcept;
    [[nodiscard]] int32_t GetHoveredIndex() const noexcept;

    void SetOnOpen (std::function<void()> cb) { m_onOpen  = std::move(cb); }
    void SetOnClose(std::function<void()> cb) { m_onClose = std::move(cb); }

private:
    void UpdateAnimation(float deltaSeconds);
    void UpdateSelection(Vec2 stick, float deltaSeconds);
    void ConfirmSelection();

    [[nodiscard]] float AngleForIndex(int32_t index) const noexcept;
    [[nodiscard]] Vec2  PositionForIndex(int32_t index, float cx, float cy, float radius) const noexcept;

    Config   m_config;
    State    m_state         = State::Hidden;
    float    m_animProgress  = 0.0f; ///< [0,~1.08] during bounce, 1.0 when Visible
    float    m_animTime      = 0.0f; ///< ms elapsed since state change
    float    m_scrimAlpha    = 0.0f; ///< [0,1] for full-screen scrim
    float    m_glowPhase     = 0.0f; ///< radians, advances at 2 Hz
    float    m_latchTimer    = 0.0f; ///< ms since stick returned to deadzone

    int32_t  m_hoveredIndex  = -1;
    int32_t  m_latchedIndex  = -1;
    int32_t  m_confirmedIndex= -1;

    std::vector<RadialMenuItem> m_items;
    std::vector<float>          m_itemScales;      ///< per-item scale for pop-in
    std::vector<float>          m_itemDelays;      ///< stagger delay (seconds)
    std::vector<float>          m_itemDelayTimers; ///< elapsed time per item

    bool m_prevSouth = false;
    bool m_prevEast  = false;

    std::function<void()> m_onOpen;
    std::function<void()> m_onClose;
};

} // namespace enjoystick::overlay
