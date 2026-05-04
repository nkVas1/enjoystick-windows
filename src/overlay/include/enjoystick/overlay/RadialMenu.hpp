#pragma once

#include <enjoystick/shared/Types.hpp>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

namespace enjoystick::overlay {

struct RadialMenuItem {
    std::wstring label;               ///< Short display label (1-6 chars)
    std::wstring icon;                ///< Unicode symbol / emoji
    std::function<void()> action;     ///< Invoked on confirm
};

class RadialMenu {
public:
    enum class State : uint8_t {
        Hidden,
        Opening,
        Visible,
        Closing,
    };

    struct Config {
        int32_t centreX = -1;  ///< -1 = screen centre
        int32_t centreY = -1;
        float   radius  = 200.0f;          ///< outer item ring radius (logical px)
        float   selectionDeadzone = 0.25f;
        float   animMs  = 140.0f;
        uint32_t colourBackground  = 0xCC1A1A2E;
        uint32_t colourItemNormal  = 0xFF2E2E4A;
        uint32_t colourItemHovered = 0xFF5A4FF3;
        uint32_t colourText        = 0xFFEEEEFF;
        uint32_t colourAccent      = 0xFF8B7FF7;
    };

    explicit RadialMenu(Config config = {});
    ~RadialMenu();

    void SetItems(std::vector<RadialMenuItem> items);
    void Open();
    void Close();

    [[nodiscard]] bool  IsVisible()      const noexcept;
    [[nodiscard]] State GetState()        const noexcept;
    [[nodiscard]] int32_t GetHoveredIndex() const noexcept;

    void Update(const ControllerState& state, float deltaSeconds);

    ///
    /// Draw the radial menu.
    ///
    /// @param renderTargetPtr  ID2D1RenderTarget* (void* to avoid d2d header in public API)
    /// @param dwriteFactoryPtr IDWriteFactory*    (void* for the same reason)
    /// @param dpiScale         System DPI / 96
    /// @param screenW/H        Logical screen dimensions (for auto-centring)
    ///
    void Draw(
        void* renderTargetPtr,
        void* dwriteFactoryPtr,
        float dpiScale,
        float screenW,
        float screenH) const;

private:
    void UpdateAnimation(float deltaSeconds);
    void UpdateSelection(Vec2 stick);
    void ConfirmSelection();

    [[nodiscard]] float AngleForIndex(int32_t index) const noexcept;
    [[nodiscard]] Vec2  PositionForIndex(int32_t index, float cx, float cy, float radius) const noexcept;

    Config                      m_config;
    std::vector<RadialMenuItem> m_items;
    State                       m_state          = State::Hidden;
    int32_t                     m_hoveredIndex   = -1;
    int32_t                     m_confirmedIndex = -1;
    float                       m_animProgress   = 0.0f;
    bool                        m_prevSouth       = false;
    bool                        m_prevEast        = false;
};

} // namespace enjoystick::overlay
