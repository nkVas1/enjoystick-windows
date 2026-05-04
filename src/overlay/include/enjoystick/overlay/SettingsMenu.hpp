#pragma once

#include <enjoystick/shared/Types.hpp>
#include <enjoystick/config/ConfigStore.hpp>
#include <enjoystick/core/DeadzoneFilter.hpp>
#include <functional>
#include <string>
#include <vector>
#include <cstdint>

namespace enjoystick::overlay {

///
/// SettingsMenu
///
/// A gamepad-driven settings panel rendered directly on the D2D overlay.
///
/// Navigation:
///   D-pad Up / Down   — select row
///   D-pad Left/Right  — adjust value (also left stick X for fine control)
///   South (A)         — toggle boolean options
///   East (B)          — close without discarding (auto-saves on every change)
///
/// Every value change is immediately forwarded to an OnChangedCallback so
/// Application can propagate it to VirtualMouse and DeadzoneFilter.
///
class SettingsMenu final {
public:
    // ------------------------------------------------------------------
    // Configuration snapshot used to initialise and reflect live values.
    // ------------------------------------------------------------------
    struct Values {
        float    cursorSpeed      = 1800.0f;
        float    curveExponent    = 1.8f;
        float    accelerationMs   = 120.0f;
        float    scrollSpeed      = 8.0f;
        float    dzInner          = 0.12f;
        float    dzOuter          = 0.90f;
        bool     triggersAsClicks = true;
        bool     useRightStick    = true;
    };

    using OnChangedCallback = std::function<void(const Values&)>;

    explicit SettingsMenu(OnChangedCallback onChange = {});
    ~SettingsMenu() = default;

    // Copy is disabled: Row structs hold raw pointers into m_values.
    SettingsMenu(const SettingsMenu&)            = delete;
    SettingsMenu& operator=(const SettingsMenu&) = delete;

    // Move is safe: pointer targets move with the object.
    SettingsMenu(SettingsMenu&&) noexcept            = default;
    SettingsMenu& operator=(SettingsMenu&&) noexcept = default;

    // ------------------------------------------------------------------
    // Callback wiring
    // ------------------------------------------------------------------

    /// Replace the on-changed callback without reconstructing the object.
    /// Call this from Application::SetupSettingsMenu() instead of
    /// doing GetSettingsMenu() = SettingsMenu(cb) which is a copy-assign.
    void SetOnChanged(OnChangedCallback onChange) {
        m_onChange = std::move(onChange);
    }

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------
    void Open(const Values& current);
    void Close();
    [[nodiscard]] bool IsOpen() const noexcept;

    // ------------------------------------------------------------------
    // Per-frame update + render
    // ------------------------------------------------------------------
    void Update(const ControllerState& state, float deltaSeconds);
    void Draw(void* renderTargetPtr, void* dwriteFactoryPtr,
              float dpiScale, float screenW, float screenH) const;

private:
    // ------------------------------------------------------------------
    // Row descriptors
    // ------------------------------------------------------------------
    enum class RowType { FloatSlider, BoolToggle };

    struct Row {
        const wchar_t* label;
        RowType        type;
        float          min, max, step;   // used for FloatSlider
        float*         fTarget;          // points into m_values — do NOT copy!
        bool*          bTarget;          // points into m_values — do NOT copy!
        const wchar_t* unit;             // e.g. L" px/s" or L""
    };

    void BuildRows();
    void AdjustSelected(float direction, bool repeat);
    void CommitChange();

    // ------------------------------------------------------------------
    // Animation
    // ------------------------------------------------------------------
    enum class State { Hidden, Opening, Visible, Closing };
    State m_state        = State::Hidden;
    float m_animProgress = 0.0f;
    void  UpdateAnimation(float deltaSeconds);

    // ------------------------------------------------------------------
    // Input state
    // ------------------------------------------------------------------
    bool  m_prevSouth  = false;
    bool  m_prevEast   = false;
    bool  m_prevDUp    = false;
    bool  m_prevDDown  = false;
    bool  m_prevDLeft  = false;
    bool  m_prevDRight = false;
    float m_repeatTimer = 0.0f;
    float m_stickX      = 0.0f;

    // ------------------------------------------------------------------
    // Content
    // ------------------------------------------------------------------
    int32_t           m_selectedRow = 0;
    Values            m_values;
    std::vector<Row>  m_rows;
    OnChangedCallback m_onChange;

    // Constants
    static constexpr float kAnimMs   = 200.0f;
    static constexpr float kRepeatHz = 8.0f;
};

} // namespace enjoystick::overlay
