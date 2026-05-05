#pragma once

#include <enjoystick/shared/Types.hpp>
#include <functional>
#include <string>
#include <vector>
#include <wrl/client.h>

interface IDWriteInlineObject;

namespace enjoystick::overlay {

// ---------------------------------------------------------------------------
// SettingsMenu
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
    using OnChangedCallback = std::function<void(const Values&)>;

    explicit SettingsMenu(OnChangedCallback onChange = nullptr);

    void Open(const Values& current);
    void Close();
    [[nodiscard]] bool IsOpen() const noexcept;

    void Update(const ControllerState& state, float dt);
    void Draw(void* renderTargetPtr, void* dwriteFactoryPtr,
              float dpiScale, float screenW, float screenH) const;

    [[nodiscard]] const Values& GetValues() const noexcept { return m_values; }
    void ResetToDefaults();

    // Set (or replace) the on-changed callback after construction.
    void SetOnChanged(OnChangedCallback cb) { m_onChange = std::move(cb); }

private:
    enum class RowType  : uint8_t { SectionHeader, FloatSlider, BoolToggle };
    enum class State    : uint8_t { Hidden, Opening, Visible, Closing };

    struct Row {
        const wchar_t* label   = nullptr;
        RowType        type    = RowType::SectionHeader;
        float          min     = 0.0f, max = 1.0f, step = 0.1f;
        float*         fTarget = nullptr;
        bool*          bTarget = nullptr;
        const wchar_t* unit    = nullptr;
    };

    void BuildRows();
    bool IsInteractiveRow(int32_t idx) const noexcept;
    int32_t NextInteractiveRow(int32_t from, int32_t dir) const noexcept;
    void AdjustSelected(float direction, bool repeat);
    void CommitChange();
    void UpdateAnimation(float dt);
    // Called whenever m_selectedRow changes to update trail state
    void OnRowChanged(int32_t newRow);

    OnChangedCallback m_onChange;
    Values m_values;
    std::vector<Row> m_rows;
    int32_t m_selectedRow  = 0;
    int32_t m_scrollOffset = 0;   // first visible row index (virtual scroll)

    State m_state        = State::Hidden;
    float m_animProgress = 0.0f;
    float m_repeatTimer  = 0.0f;

    // -----------------------------------------------------------------------
    // Navigation timing (all in seconds)
    //
    // kSnapFirst     : hold duration before auto-repeat begins.
    // kSnapNext      : base auto-repeat interval (springy phase).
    // kSnapFast      : minimum repeat interval after kNavAccelStart of hold.
    // kNavAccelStart : seconds of hold before interval starts shrinking.
    // kNavAccelRange : range over which it blends to kSnapFast.
    // kNavDeadzone   : stick deflection required to start navigating.
    // -----------------------------------------------------------------------
    static constexpr float kSnapFirst      = 0.85f;  // was 0.62
    static constexpr float kSnapNext       = 0.28f;  // was 0.20
    static constexpr float kSnapFast       = 0.065f; // was 0.055
    static constexpr float kNavAccelStart  = 1.2f;   // was 1.0
    static constexpr float kNavAccelRange  = 0.80f;
    static constexpr float kNavDeadzone    = 0.55f;
    static constexpr float kAnimMs         = 220.0f;

    bool  m_stickNavActive    = false;
    float m_stickNavCooldown  = 0.0f;
    float m_stickNavHoldTime  = 0.0f;   // accumulated hold time for accel
    bool  m_stickLxActive     = false;
    float m_stickLxCooldown   = 0.0f;
    bool  m_dpadVertHeld      = false;
    float m_dpadVertTimer     = 0.0f;
    bool  m_dpadHorzHeld      = false;
    float m_dpadHorzTimer     = 0.0f;

    bool m_prevSouth = false, m_prevEast  = false, m_prevNorth = false;
    bool m_prevDUp   = false, m_prevDDown = false;
    bool m_prevDLeft = false, m_prevDRight= false;

    // -----------------------------------------------------------------------
    // Trail / transition animation state.
    //
    // m_prevRow    : the row we were on before the last navigation step.
    // m_trailAlpha : 0..1, decays to 0 over kTrailDecayMs after each hop.
    // m_selAnimT   : 0..1 spring-like progress since last hop.
    // -----------------------------------------------------------------------
    static constexpr float kTrailDecayMs  = 160.0f;
    static constexpr float kSelAnimSpeed  = 8.0f;
    mutable int32_t m_prevRow    = -1;
    mutable float   m_trailAlpha = 0.0f;
    mutable float   m_selAnimT   = 1.0f;

    // DWrite ellipsis for text overflow trimming (mutable = lazy-init in Draw)
    mutable Microsoft::WRL::ComPtr<IDWriteInlineObject> m_dwriteEllipsis;
};

} // namespace enjoystick::overlay
