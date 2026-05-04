#pragma once

#include <enjoystick/shared/Types.hpp>

#include <functional>
#include <string>
#include <vector>

namespace enjoystick::overlay {

// ---------------------------------------------------------------------------
// RadialMenuItem
// ---------------------------------------------------------------------------
struct RadialMenuItem {
    std::wstring          label;
    std::wstring          icon;
    std::function<void()> action;
    std::wstring          id;

    RadialMenuItem() = default;
    RadialMenuItem(std::wstring lbl, std::wstring ico, std::function<void()> act,
                   std::wstring identifier = {})
        : label(std::move(lbl))
        , icon (std::move(ico))
        , action(std::move(act))
        , id   (std::move(identifier))
    {}
};

// ---------------------------------------------------------------------------
// RadialMenu
// ---------------------------------------------------------------------------
class RadialMenu {
public:
    enum class State { Hidden, Opening, Visible, Closing };

    struct Config {
        float   radius              = 185.0f;
        float   selectionDeadzone  = 0.25f;
        float   latchMs            = 800.0f;
        float   openAnimMs         = 260.0f;
        float   closeAnimMs        = 160.0f;
        int32_t centreX            = -1;
        int32_t centreY            = -1;
        bool    showActiveIndicator = true;
    };

    explicit RadialMenu(Config config = {});
    ~RadialMenu();

    // ---- Items --------------------------------------------------------------
    void SetItems(std::vector<RadialMenuItem> items);
    [[nodiscard]] const std::vector<RadialMenuItem>& GetItems() const noexcept { return m_items; }

    // ---- Lifecycle ----------------------------------------------------------
    void Open();
    void Close();
    [[nodiscard]] bool    IsVisible()      const noexcept;
    [[nodiscard]] State   GetState()        const noexcept;
    [[nodiscard]] int32_t GetHoveredIndex() const noexcept;

    // ---- Callbacks ----------------------------------------------------------
    void SetOnOpen (std::function<void()> cb) { m_onOpen  = std::move(cb); }
    void SetOnClose(std::function<void()> cb) { m_onClose = std::move(cb); }

    // ---- Frame --------------------------------------------------------------
    void Update(const ControllerState& state, float deltaSeconds);
    void Draw  (void* renderTargetPtr, void* dwriteFactoryPtr,
                float dpiScale, float screenW, float screenH) const;

private:
    void UpdateAnimation(float deltaSeconds);
    void UpdateSelection(Vec2 stick, float deltaSeconds);
    void ConfirmSelection();

    [[nodiscard]] float AngleForIndex   (int32_t index) const noexcept;
    [[nodiscard]] Vec2  PositionForIndex(int32_t index, float cx, float cy, float radius) const noexcept;

    Config                      m_config;
    std::vector<RadialMenuItem> m_items;
    std::vector<float>          m_itemScales;
    std::vector<float>          m_itemDelays;
    std::vector<float>          m_itemDelayTimers;

    State   m_state         = State::Hidden;
    float   m_animTime      = 0.0f;
    float   m_animProgress  = 0.0f;
    float   m_scrimAlpha    = 0.0f;
    float   m_glowPhase     = 0.0f;
    float   m_latchTimer    = 0.0f;

    // Confirmation flash (brief white burst before close)
    float   m_flashTimer    = 0.0f;
    int32_t m_flashIndex    = -1;

    int32_t m_hoveredIndex   = -1;
    int32_t m_latchedIndex   = -1;
    int32_t m_confirmedIndex = -1;

    bool m_prevSouth = false;
    bool m_prevEast  = false;

    std::function<void()> m_onOpen;
    std::function<void()> m_onClose;
};

} // namespace enjoystick::overlay
