#pragma once

#include <enjoystick/shared/Types.hpp>

#include <functional>
#include <string>
#include <vector>

namespace enjoystick::overlay {

class SettingsMenu {
public:
    struct Values {
        float cursorSpeed      = 6.0f;
        float curveExponent    = 1.35f;
        float accelerationMs   = 80.0f;
        float scrollSpeed      = 4.0f;
        float dzInner          = 0.08f;
        float dzOuter          = 0.98f;
        bool  triggersAsClicks = false;
        bool  useRightStick    = true;
        bool  adaptiveSpeed      = true;
        float targetTraversalMs  = 900.0f;
        float dpiWeight          = 0.5f;
    };

    using OnChangedCallback = std::function<void(const Values&)>;

    explicit SettingsMenu(OnChangedCallback onChange = nullptr);

    void Open (const Values& current);
    void Close();
    [[nodiscard]] bool IsOpen() const noexcept;

    void Update(const ControllerState& state, float deltaSeconds);
    void Draw  (void* renderTargetPtr, void* dwriteFactoryPtr,
                float dpiScale, float screenW, float screenH) const;

    void SetOnChanged(OnChangedCallback cb) { m_onChange = std::move(cb); }

private:
    enum class RowType : uint8_t { FloatSlider, BoolToggle, SectionHeader };
    struct Row {
        const wchar_t* label   = nullptr;
        RowType        type    = RowType::FloatSlider;
        float          min     = 0.0f;
        float          max     = 1.0f;
        float          step    = 0.1f;
        float*         fTarget = nullptr;
        bool*          bTarget = nullptr;
        const wchar_t* unit    = L"";
    };

    enum class State : uint8_t { Hidden, Opening, Visible, Closing };

    void BuildRows();
    void UpdateAnimation(float deltaSeconds);
    void AdjustSelected(float direction, bool repeat);
    void CommitChange();
    void ResetToDefaults();

    [[nodiscard]] int32_t NextInteractiveRow(int32_t from, int32_t dir) const noexcept;
    [[nodiscard]] bool    IsInteractiveRow(int32_t idx) const noexcept;

    static constexpr float kAnimMs   = 160.0f;
    static constexpr float kRepeatHz = 8.0f;

    // Stick snap (magnet) navigation
    // kSnapFirst: how long the stick must be held before the first auto-repeat
    // kSnapNext:  interval between subsequent auto-repeat steps
    // kNavDeadzone: stick threshold to trigger vertical navigation
    static constexpr float kSnapFirst   = 0.38f;   // seconds
    static constexpr float kSnapNext    = 0.22f;   // seconds (slower = more deliberate)
    static constexpr float kNavDeadzone = 0.40f;   // stick magnitude threshold

    OnChangedCallback m_onChange;
    std::vector<Row>  m_rows;
    Values            m_values;
    int32_t           m_selectedRow  = 0;
    State             m_state        = State::Hidden;
    float             m_animProgress = 0.0f;
    float             m_repeatTimer  = 0.0f;

    // Stick snap state for vertical row navigation
    bool  m_stickNavActive    = false;
    float m_stickNavCooldown  = 0.0f;

    bool m_prevSouth  = false;
    bool m_prevEast   = false;
    bool m_prevNorth  = false;
    bool m_prevDUp    = false;
    bool m_prevDDown  = false;
    bool m_prevDLeft  = false;
    bool m_prevDRight = false;
};

} // namespace enjoystick::overlay
