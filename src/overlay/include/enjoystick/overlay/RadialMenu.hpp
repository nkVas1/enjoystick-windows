#pragma once

#include <enjoystick/shared/Types.hpp>
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace enjoystick::overlay {

///
/// RadialMenu — a controller-friendly circular menu for quick actions and text input.
///
/// Layout: items arranged in a circle, selection driven by right-stick angle.
/// Confirmed with South (A/Cross) or trigger. Cancelled with East (B/Circle).
///
/// The same widget doubles as a quick-action ring (apps, system shortcuts)
/// and as a radial keyboard grid for text entry.
///
struct RadialMenuItem {
    std::wstring label;     ///< Display label (short, 1-4 chars recommended)
    std::wstring icon;      ///< Optional Unicode symbol / emoji
    std::function<void()> action; ///< Invoked on confirm
};

class RadialMenu {
public:
    enum class State : uint8_t {
        Hidden,
        Opening,   ///< Scale-in animation running
        Visible,
        Closing,   ///< Scale-out animation running
    };

    struct Config {
        /// Centre position in screen coordinates (-1 = screen centre)
        int32_t centreX = -1;
        int32_t centreY = -1;

        /// Outer radius in logical pixels
        float radius = 200.0f;

        /// Dead-zone magnitude of the selection stick (below = no item highlighted)
        float selectionDeadzone = 0.25f;

        /// Animation duration in milliseconds
        float animMs = 140.0f;

        /// Colour scheme (ABGR)
        uint32_t colourBackground   = 0xCC1A1A2E;  ///< Deep navy, semi-transparent
        uint32_t colourItemNormal   = 0xFF2E2E4A;
        uint32_t colourItemHovered  = 0xFF5A4FF3;  ///< Enjoystick violet accent
        uint32_t colourText         = 0xFFEEEEFF;
        uint32_t colourAccent       = 0xFF8B7FF7;
    };

    explicit RadialMenu(Config config = {});
    ~RadialMenu();

    /// Replace the current item ring.
    void SetItems(std::vector<RadialMenuItem> items);

    /// Open/close the menu with animation.
    void Open();
    void Close();
    [[nodiscard]] bool IsVisible() const noexcept;
    [[nodiscard]] State GetState() const noexcept;

    /// Feed controller state every frame.
    void Update(const ControllerState& state, float deltaSeconds);

    /// Draw using Direct2D DC provided by OverlayWindow.
    /// Caller must have called BeginDraw() already.
    void Draw(void* d2dRenderTarget, float dpiScale) const;

    /// Returns index of currently highlighted item, or -1 if none.
    [[nodiscard]] int32_t GetHoveredIndex() const noexcept;

private:
    void UpdateAnimation(float deltaSeconds);
    void UpdateSelection(Vec2 stick);
    void ConfirmSelection();

    [[nodiscard]] float AngleForIndex(int32_t index) const noexcept;
    [[nodiscard]] Vec2  PositionForIndex(int32_t index, float scale) const noexcept;

    Config                     m_config;
    std::vector<RadialMenuItem> m_items;
    State                      m_state          = State::Hidden;
    int32_t                    m_hoveredIndex   = -1;
    int32_t                    m_confirmedIndex = -1;

    float m_animProgress  = 0.0f;  ///< [0, 1] — 0 = hidden, 1 = fully open

    // Button edge tracking
    bool  m_prevSouth     = false;
    bool  m_prevEast      = false;
    bool  m_prevGuide     = false;
};

} // namespace enjoystick::overlay
