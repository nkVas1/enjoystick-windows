#pragma once

#include <enjoystick/shared/Types.hpp>
#include <enjoystick/overlay/Overlay_SpringAnim.hpp>
#include <functional>
#include <string>
#include <vector>
#include <wrl/client.h>

interface IDWriteInlineObject;

namespace enjoystick::overlay {

enum class HapticType : uint8_t;

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
    using OnHapticCallback  = std::function<void(HapticType)>;

    explicit SettingsMenu(OnChangedCallback onChange = nullptr);

    void Open(const Values& current);
    void Close();
    [[nodiscard]] bool IsOpen() const noexcept;

    void Update(const ControllerState& state, float dt);
    void Draw(void* renderTargetPtr, void* dwriteFactoryPtr,
              float dpiScale, float screenW, float screenH) const;

    [[nodiscard]] const Values& GetValues() const noexcept { return m_values; }
    void ResetToDefaults();

    void SetOnChanged(OnChangedCallback cb) { m_onChange = std::move(cb); }
    void SetOnHaptic (OnHapticCallback  cb) { m_onHaptic = std::move(cb); }

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
    void FireHaptic(HapticType type);
    void UpdateAnimation(float dt);
    void OnRowChanged(int32_t newRow);
    void SyncToggleSprings();  // snap springs to current bool values

    OnChangedCallback m_onChange;
    OnHapticCallback  m_onHaptic;
    Values m_values;
    std::vector<Row> m_rows;
    int32_t m_selectedRow  = 0;
    int32_t m_scrollOffset = 0;

    // One spring per row — only BoolToggle rows actually use theirs.
    // Indexed in parallel with m_rows.  Rebuilt in BuildRows() / Open().
    mutable std::vector<FloatSpring> m_toggleKnobPositions;

    static constexpr int32_t kVisibleRows = 10;

    State m_state        = State::Hidden;
    float m_animProgress = 0.0f;
    float m_repeatTimer  = 0.0f;

    // -----------------------------------------------------------------------
    // Navigation timing
    // -----------------------------------------------------------------------
    static constexpr float kSnapFirst      = 1.50f;
    static constexpr float kSnapNext       = 0.40f;
    static constexpr float kSnapFast       = 0.085f;
    static constexpr float kNavAccelStart  = 1.6f;
    static constexpr float kNavAccelRange  = 0.80f;
    static constexpr float kNavDeadzone    = 0.72f;
    static constexpr float kAnimMs         = 160.0f;

    bool  m_stickNavActive    = false;
    float m_stickNavCooldown  = 0.0f;
    float m_stickNavHoldTime  = 0.0f;
    bool  m_stickLxActive     = false;
    float m_stickLxCooldown   = 0.0f;
    bool  m_dpadVertHeld      = false;
    float m_dpadVertTimer     = 0.0f;
    bool  m_dpadHorzHeld      = false;
    float m_dpadHorzTimer     = 0.0f;

    bool m_prevSouth = false, m_prevEast  = false, m_prevNorth = false;
    bool m_prevDUp   = false, m_prevDDown = false;
    bool m_prevDLeft = false, m_prevDRight= false;

    static constexpr float kTrailDecayMs  = 160.0f;
    static constexpr float kSelAnimSpeed  = 10.0f;
    mutable int32_t m_prevRow    = -1;
    mutable float   m_trailAlpha = 0.0f;
    mutable float   m_selAnimT   = 1.0f;

    mutable Microsoft::WRL::ComPtr<IDWriteInlineObject> m_dwriteEllipsis;
};

} // namespace enjoystick::overlay
