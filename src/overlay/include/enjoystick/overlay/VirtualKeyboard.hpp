#pragma once

#include <enjoystick/shared/Types.hpp>
#include <enjoystick/overlay/Overlay_SpringAnim.hpp>

#include <functional>
#include <string>
#include <vector>

namespace enjoystick::overlay {

// ---------------------------------------------------------------------------
// VirtualKeyboard  (Steam-quality UX target)
//
// Controls:
//   Left stick / DPad  - move cursor; magnetically snaps to keys
//   Right stick        - proximity hover (nearest key within radius)
//   South (A/Cross)    - type highlighted key; hold to auto-repeat
//   West  (X/Square)   - backspace; hold to auto-repeat with accel
//   East  (B/Circle)   - close / cancel
//   North (Y/Tri)      - confirm / submit text
//   LB                 - cycle layer: Alpha -> Cyr -> Sym -> Alpha  (debounced)
//   RB                 - return to Alpha layer
//   L3 (click)         - toggle Caps Lock
//
// Axis convention (XInput):
//   lx > 0 = right, lx < 0 = left
//   ly > 0 = UP,    ly < 0 = DOWN
// ---------------------------------------------------------------------------

class VirtualKeyboard {
public:
    using OnCharCallback     = std::function<void(wchar_t ch)>;
    using OnSubmitCallback   = std::function<void(const std::wstring& text)>;
    using OnNavigateCallback = std::function<void()>;  // fired on every key hop (haptic)
    using OnTypeCallback     = std::function<void()>;  // fired on every char typed (haptic)

    enum class Layer : uint8_t { Alpha, Cyr, Sym };

    VirtualKeyboard() = default;
    ~VirtualKeyboard() = default;

    // ---- Lifecycle ----------------------------------------------------------
    void Open (const std::wstring& seed = L"");
    void Close();
    [[nodiscard]] bool IsOpen() const noexcept;

    // ---- Callbacks ----------------------------------------------------------
    void SetOnChar    (OnCharCallback     cb) { m_onChar     = std::move(cb); }
    void SetOnSubmit  (OnSubmitCallback   cb) { m_onSubmit   = std::move(cb); }
    void SetOnNavigate(OnNavigateCallback cb) { m_onNavigate = std::move(cb); }
    void SetOnType    (OnTypeCallback     cb) { m_onType     = std::move(cb); }

    // ---- Frame --------------------------------------------------------------
    void Update(const ControllerState& state, float deltaSeconds);
    void Draw  (void* renderTargetPtr,
                void* dwriteFactoryPtr,
                float dpiScale,
                float screenW,
                float screenH) const;

    // ---- Text / layer state (read-only) ------------------------------------
    [[nodiscard]] const std::wstring& GetText()  const noexcept { return m_text; }
    [[nodiscard]] Layer               GetLayer() const noexcept { return m_layer; }
    [[nodiscard]] bool                GetCaps()  const noexcept { return m_caps;  }

    [[nodiscard]] const wchar_t* GetLayerName() const noexcept {
        switch (m_layer) {
            case Layer::Cyr: return L"CYR";
            case Layer::Sym: return L"SYM";
            default:         return m_caps ? L"CAPS" : L"ALPHA";
        }
    }

private:
    // ---- Key grid -----------------------------------------------------------
    struct Key {
        std::wstring label;
        std::wstring shiftLabel;
        std::wstring symLabel;
        std::wstring cyrLabel;
        std::wstring cyrShift;
        float        widthMul  = 1.0f;
        bool         isSpecial = false;
    };

    enum class State : uint8_t { Hidden, Opening, Visible, Closing };

    void BuildLayout();

    std::vector<std::vector<Key>> m_rows;

    int32_t m_row = 0;
    int32_t m_col = 0;

    // -------------------------------------------------------------------------
    // Left-stick navigation timing (seconds)
    //
    // kSnapDeadzone 0.55: moderate stick push required (good balance).
    //
    // kStickRepeatFirst 0.50s: short initial delay before auto-repeat starts.
    // kStickRepeatNext  0.14s: comfortable repeat cadence.
    // kStickRepeatFast  0.055s: maximum repeat rate after sustained hold.
    // -------------------------------------------------------------------------
    float  m_stickCooldown    = 0.0f;
    float  m_stickHoldTime    = 0.0f;
    static constexpr float kStickRepeatFirst      = 0.50f;
    static constexpr float kStickRepeatNext       = 0.14f;
    static constexpr float kStickRepeatFast       = 0.055f;
    static constexpr float kStickRepeatAccelStart = 0.80f;
    static constexpr float kStickRepeatAccelRange = 0.60f;
    static constexpr float kSnapDeadzone          = 0.55f;
    bool   m_stickActive = false;

    static constexpr float kDPadFirst = 0.32f;
    static constexpr float kDPadNext  = 0.09f;
    bool    m_dpadHeld    = false;
    float   m_dpadTimer   = 0.0f;
    int32_t m_dpadDirRow  = 0;
    int32_t m_dpadDirCol  = 0;

    static constexpr float kButtonBounceGuardMs = 80.0f;

    static constexpr float kTypeHoldFirstMs    = 900.0f;
    static constexpr float kTypeHoldRepeatMs   = 120.0f;
    static constexpr float kTypeHoldFastMs     =  55.0f;
    static constexpr float kTypeHoldAccelStart =   1.2f;
    static constexpr float kTypeHoldAccelRange =   0.8f;
    float  m_southHeldMs      = 0.0f;
    float  m_southRepeatCd    = 0.0f;
    float  m_southReleaseGuard= 0.0f;
    bool   m_southInRepeat    = false;

    static constexpr float kWestHoldFirstMs    = 800.0f;
    static constexpr float kWestHoldRepeatMs   = 110.0f;
    static constexpr float kWestHoldFastMs     =  50.0f;
    static constexpr float kWestHoldAccelStart =   1.0f;
    static constexpr float kWestHoldAccelRange =   0.7f;
    float  m_westHeldMs       = 0.0f;
    float  m_westRepeatCd     = 0.0f;
    float  m_westReleaseGuard = 0.0f;
    bool   m_westInRepeat     = false;

    static constexpr float kTypeDebounceMs  = 220.0f;
    static constexpr float kWestDebounceMs  = 380.0f;
    static constexpr float kLbDebounceMs    = 400.0f;
    float m_typeDebounce = 0.0f;
    float m_westDebounce = 0.0f;
    float m_lbDebounce   = 0.0f;

    static constexpr float kRightStickDz       = 0.25f;
    static constexpr float kProximityRadius    = 120.0f;
    mutable float m_screenW = 1920.0f;
    mutable float m_screenH = 1080.0f;
    mutable float m_dpiScale= 1.0f;

    State  m_state     = State::Hidden;
    float  m_glowPhase = 0.0f;
    Layer  m_layer     = Layer::Alpha;
    bool   m_shift     = false;
    bool   m_caps      = false;

    mutable FloatSpring m_panelSpring;
    mutable FloatSpring m_cursorSpringX;
    mutable FloatSpring m_cursorSpringY;
    mutable FloatSpring m_trailSpringX;
    mutable FloatSpring m_trailSpringY;
    mutable FloatSpring m_cursorScaleSpring;

    std::wstring m_text;

    OnCharCallback     m_onChar;
    OnSubmitCallback   m_onSubmit;
    OnNavigateCallback m_onNavigate;
    OnTypeCallback     m_onType;

    bool m_prevSouth = false;
    bool m_prevEast  = false;
    bool m_prevWest  = false;
    bool m_prevNorth = false;
    bool m_prevLB    = false;
    bool m_prevRB    = false;
    bool m_prevLS    = false;

    [[nodiscard]] const Key* CurrentKey() const noexcept;
    [[nodiscard]] std::wstring KeyDisplay(const Key& k) const;
    bool TypeKey(const Key& k);
    void NavigateTo(int32_t row, int32_t col);
    [[nodiscard]] int32_t RowKeyCount(int32_t row) const noexcept;
    [[nodiscard]] Vec2    KeyCentrePixel(int32_t row, int32_t col,
                                         float dpiScale,
                                         float screenW, float screenH) const noexcept;
};

} // namespace enjoystick::overlay
