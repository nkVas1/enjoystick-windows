#pragma once

#include <enjoystick/shared/Types.hpp>

#include <dwrite.h>
#include <wrl/client.h>
#include <functional>
#include <string>
#include <vector>

namespace enjoystick::overlay {

class SettingsMenu {
public:
    struct Values {
        float cursorSpeed        = 6.0f;
        float curveExponent      = 1.35f;
        float accelerationMs     = 80.0f;
        float scrollSpeed        = 4.0f;
        float dzInner            = 0.08f;
        float dzOuter            = 0.98f;
        bool  triggersAsClicks   = false;
        bool  useRightStick      = true;
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

    static constexpr float kAnimMs      = 160.0f;
    // Magnetic snap timings:
    // kSnapFirst: initial delay before auto-repeat kicks in.
    //   Longer = stronger 'magnetic' feel; a tap always moves exactly one step.
    // kSnapNext:  interval between steps once auto-repeat is active.
    //   Short enough for fast navigation, long enough to count steps.
    // kNavDeadzone: analogue stick centre deadzone.
    //   Raised slightly so a gently resting stick never drifts selection.
    static constexpr float kSnapFirst   = 0.50f;
    static constexpr float kSnapNext    = 0.14f;
    static constexpr float kNavDeadzone = 0.48f;

    OnChangedCallback m_onChange;
    std::vector<Row>  m_rows;
    Values            m_values;
    int32_t           m_selectedRow  = 0;
    State             m_state        = State::Hidden;
    float             m_animProgress = 0.0f;
    float             m_repeatTimer  = 0.0f;

    // Left-stick vertical snap navigation
    bool  m_stickNavActive   = false;
    float m_stickNavCooldown = 0.0f;

    // Left-stick horizontal fine-tune (mutually exclusive with Y-nav)
    bool  m_stickLxActive   = false;
    float m_stickLxCooldown = 0.0f;

    // DPad vertical navigation
    bool  m_dpadVertHeld  = false;
    float m_dpadVertTimer = 0.0f;

    // DPad horizontal slider adjustment
    bool  m_dpadHorzHeld  = false;
    float m_dpadHorzTimer = 0.0f;

    bool m_prevSouth  = false;
    bool m_prevEast   = false;
    bool m_prevNorth  = false;
    bool m_prevDUp    = false;
    bool m_prevDDown  = false;
    bool m_prevDLeft  = false;
    bool m_prevDRight = false;

    // Mutable: lazily created in Draw() for label ellipsis trimming
    mutable Microsoft::WRL::ComPtr<IDWriteInlineObject> m_dwriteEllipsis;
};

} // namespace enjoystick::overlay
