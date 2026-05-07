#pragma once

#include <enjoystick/shared/Types.hpp>
#include <functional>
#include <string>
#include <vector>
#include <wrl/client.h>

#include "Overlay_SpringAnim.hpp"

interface IDWriteInlineObject;

namespace enjoystick::overlay {

// ---------------------------------------------------------------------------
// SettingsMenu  — tab-based layout, no scrolling
// ---------------------------------------------------------------------------
class SettingsMenu {
public:
    struct Values {
        float cursorSpeed       = 8.0f;
        float curveExponent     = 1.5f;
        float accelerationMs    = 120.0f;
        bool  useRightStick     = false;
        float scrollSpeed       = 12.0f;
        bool  triggersAsClicks  = false;
        bool  adaptiveSpeed     = false;
        float targetTraversalMs = 800.0f;
        float dpiWeight         = 0.5f;
        float dzInner           = 0.12f;
        float dzOuter           = 0.92f;
    };

    // Action IDs fired via OnAction callback
    enum class ActionId : uint8_t { SaveProfile, LoadProfile };

    // Row types — public so that free helpers in SettingsMenu.cpp can use them
    enum class RowType  : uint8_t { FloatSlider, BoolToggle, ActionButton };

    struct Row {
        const wchar_t* label   = nullptr;
        RowType        type    = RowType::FloatSlider;
        float          min     = 0.0f, max = 1.0f, step = 0.1f;
        float*         fTarget = nullptr;
        bool*          bTarget = nullptr;
        const wchar_t* unit    = nullptr;
        ActionId       actionId = ActionId::SaveProfile;
    };

    // Tab descriptor — public so that FlatRowIndex() in SettingsMenu.cpp compiles
    struct Tab {
        const wchar_t*   title;
        std::vector<Row> rows;
    };

    using OnChangedCallback  = std::function<void(const Values&)>;
    using OnNavigateCallback = std::function<void()>;
    using OnAdjustCallback   = std::function<void()>;
    using OnActionCallback   = std::function<void(ActionId)>;

    explicit SettingsMenu(OnChangedCallback onChange = nullptr);

    void Open(const Values& current);
    void Close();
    [[nodiscard]] bool IsOpen() const noexcept;

    void Update(const ControllerState& state, float dt);
    void Draw(void* renderTargetPtr, void* dwriteFactoryPtr,
              float dpiScale, float screenW, float screenH) const;

    [[nodiscard]] const Values& GetValues() const noexcept { return m_values; }
    void ResetToDefaults();

    void SetOnChanged (OnChangedCallback  cb) { m_onChange   = std::move(cb); }
    void SetOnNavigate(OnNavigateCallback cb) { m_onNavigate = std::move(cb); }
    void SetOnAdjust  (OnAdjustCallback   cb) { m_onAdjust   = std::move(cb); }
    void SetOnAction  (OnActionCallback   cb) { m_onAction   = std::move(cb); }

private:
    enum class State    : uint8_t { Hidden, Opening, Visible, Closing };

    void BuildTabs();
    bool IsInteractiveRow(int32_t tabIdx, int32_t rowIdx) const noexcept;
    int32_t NextInteractiveRow(int32_t tabIdx, int32_t from, int32_t dir) const noexcept;
    void AdjustSelected(float direction, bool repeat);
    void CommitChange();
    void ActivateSelected();
    void UpdateAnimation(float dt);
    void OnRowChanged(int32_t newRow);
    void RebuildToggleSprings();
    void SwitchTab(int32_t dir);

    OnChangedCallback  m_onChange;
    OnNavigateCallback m_onNavigate;
    OnAdjustCallback   m_onAdjust;
    OnActionCallback   m_onAction;

    Values m_values;
    std::vector<Tab> m_tabs;
    int32_t m_activeTab    = 0;
    int32_t m_selectedRow  = 0;

    State m_state        = State::Hidden;
    float m_animProgress = 0.0f;

    // -----------------------------------------------------------------------
    // Navigation timing — single-step policy with hysteresis
    //
    // HYSTERESIS (same fix as VirtualKeyboard, 2026-05-07):
    //   kSnapDeadzone  = 0.62 — axis becomes active (first step fires)
    //   kNavRelease    = 0.28 — axis resets (next flick fires first step)
    //   The gap [0.28, 0.62] is the hysteresis band; the stick must fully
    //   retreat below kNavRelease before a new first-step can fire.
    //
    // kSnapGate:    hold this many seconds before auto-repeat begins.
    // kSnapCadence: flat repeat interval after the gate.
    // -----------------------------------------------------------------------
    static constexpr float kSnapDeadzone  = 0.62f;   // activate threshold
    static constexpr float kNavRelease    = 0.28f;   // deactivate threshold (hysteresis)
    static constexpr float kNavDeadzone   = 0.62f;   // alias for value-adjust axis
    static constexpr float kSnapGate      = 1.00f;   // s before first repeat (was 1.5)
    static constexpr float kSnapCadence   = 0.22f;   // s between repeats (flat)
    static constexpr float kAnimMs        = 160.0f;

    // Y-axis (row navigation) — hysteresis pair
    bool  m_stickNavActive    = false;
    bool  m_stickNavWasActive = false;  // true while stick is at or above deadzone
    float m_stickNavCooldown  = 0.0f;
    float m_stickNavHoldTime  = 0.0f;

    // X-axis (tab switch / value adjust) — hysteresis pair
    bool  m_stickLxActive     = false;
    bool  m_stickLxWasActive  = false;  // true while stick is at or above deadzone
    float m_stickLxCooldown   = 0.0f;

    bool  m_dpadVertHeld      = false;
    float m_dpadVertTimer     = 0.0f;

    bool  m_dpadHorzHeld      = false;
    float m_dpadHorzTimer     = 0.0f;

    bool  m_lbHeld  = false;
    bool  m_rbHeld  = false;
    bool  m_prevLB  = false;
    bool  m_prevRB  = false;

    bool m_prevSouth = false, m_prevEast  = false, m_prevNorth = false;
    bool m_prevDUp   = false, m_prevDDown = false;
    bool m_prevDLeft = false, m_prevDRight= false;

    mutable FloatSpring m_selCursorSpring;
    mutable bool        m_selCursorInit = false;

    mutable FloatSpring m_tabIndicatorSpring;
    mutable bool        m_tabIndicatorInit = false;

    static constexpr float kTrailDecayMs  = 160.0f;
    static constexpr float kSelAnimSpeed  = 10.0f;
    mutable int32_t m_prevRow    = -1;
    mutable float   m_trailAlpha = 0.0f;
    mutable float   m_selAnimT   = 1.0f;

    mutable std::vector<FloatSpring> m_toggleSprings;

    mutable float m_actionFlashAlpha = 0.0f;
    mutable int32_t m_actionFlashRow = -1;

    mutable Microsoft::WRL::ComPtr<IDWriteInlineObject> m_dwriteEllipsis;
};

} // namespace enjoystick::overlay
