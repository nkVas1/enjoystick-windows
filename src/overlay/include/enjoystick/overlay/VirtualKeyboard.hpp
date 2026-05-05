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
//   Right stick        - proximity hover (nearest key selected by distance)
//   South (A/Cross)    - type highlighted key  [hold: accelerating auto-repeat]
//   West  (X/Square)   - backspace             [hold: accelerating auto-repeat]
//   East  (B/Circle)   - close / cancel
//   North (Y/Tri)      - confirm / submit text
//   LB                 - cycle layer: Alpha -> Cyr -> Sym -> Alpha  (debounced)
//   RB                 - return to Alpha layer
//   L3 (click)         - toggle Caps Lock
//
// Text bar removed: chars go directly to focused input via SendInput.
// ---------------------------------------------------------------------------

class VirtualKeyboard {
public:
    using OnCharCallback     = std::function<void(wchar_t ch)>;
    using OnSubmitCallback   = std::function<void(const std::wstring& text)>;
    using OnNavigateCallback = std::function<void()>; // fires on every key-step

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
    // Called on every navigation step and on every key type/backspace.
    // Application wires this to a short XInput rumble pulse.
    void SetOnNavigate(OnNavigateCallback cb) { m_onNavigate = std::move(cb); }

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

    // =========================================================================
    // Left-stick / DPad navigation timing
    // =========================================================================
    float  m_stickCooldown = 0.0f;
    float  m_stickHoldTime = 0.0f;
    bool   m_stickActive   = false;

    // Single-flick safety: kStickRepeatFirst is long enough that a gentle
    // flick-and-release is guaranteed to move exactly ONE key.
    static constexpr float kStickRepeatFirst      = 1.40f; // s — first-step window
    static constexpr float kStickRepeatNext       = 0.28f; // s — base auto-repeat
    static constexpr float kStickRepeatFast       = 0.07f; // s — max speed
    static constexpr float kStickRepeatAccelStart = 1.80f; // s — hold before accel
    static constexpr float kStickRepeatAccelRange = 1.00f; // s — blend range
    static constexpr float kSnapDeadzone          = 0.78f; // strong centre magnet

    static constexpr float kDPadFirst = 0.65f; // s — DPad initial delay
    static constexpr float kDPadNext  = 0.18f; // s — DPad repeat interval
    bool    m_dpadHeld   = false;
    float   m_dpadTimer  = 0.0f;
    int32_t m_dpadDirRow = 0;
    int32_t m_dpadDirCol = 0;

    // =========================================================================
    // Hold-to-repeat: South button (type key)
    //
    // kTypeHoldFirst  — first repeat fires after this many seconds
    // kTypeHoldNext   — initial repeat interval
    // kTypeHoldFast   — minimum repeat interval after accel
    // kTypeAccelStart — hold this long before interval starts shrinking
    // kTypeAccelRange — blend range (seconds) to reach kTypeHoldFast
    // =========================================================================
    static constexpr float kTypeHoldFirst   = 0.55f;
    static constexpr float kTypeHoldNext    = 0.14f;
    static constexpr float kTypeHoldFast    = 0.055f;
    static constexpr float kTypeAccelStart  = 1.20f;
    static constexpr float kTypeAccelRange  = 0.70f;

    bool  m_southHeld      = false;
    float m_southHoldTimer = 0.0f;  // accumulated hold duration
    float m_southHoldRepeat= 0.0f;  // countdown to next fire
    float m_southHoldPhase = 0.0f;  // reserved

    // =========================================================================
    // Hold-to-repeat: West button (backspace)
    // =========================================================================
    static constexpr float kBackspaceHoldFirst   = 0.42f;
    static constexpr float kBackspaceHoldNext    = 0.12f;
    static constexpr float kBackspaceHoldFast    = 0.040f;
    static constexpr float kBackspaceAccelStart  = 0.90f;
    static constexpr float kBackspaceAccelRange  = 0.60f;

    bool  m_westHeld      = false;
    float m_westHoldTimer = 0.0f;
    float m_westHoldRepeat= 0.0f;
    float m_westHoldPhase = 0.0f;

    // =========================================================================
    // Layer-switch debounce (LB)
    // =========================================================================
    static constexpr float kLbDebounceMs = 320.0f;
    float m_lbDebounce = 0.0f;

    // Kept for compat — no longer used for type/backspace gating
    float m_typeDebounce = 0.0f;
    float m_westDebounce = 0.0f;

    State  m_state     = State::Hidden;
    float  m_glowPhase = 0.0f;
    Layer  m_layer     = Layer::Alpha;
    bool   m_shift     = false;
    bool   m_caps      = false;

    mutable FloatSpring m_panelSpring;
    mutable FloatSpring m_cursorSpringX;
    mutable FloatSpring m_cursorSpringY;
    // Liquid trail: low-stiffness, heavily damped — lags visibly behind cursor
    mutable FloatSpring m_trailSpringX;
    mutable FloatSpring m_trailSpringY;
    mutable FloatSpring m_cursorScaleSpring;

    std::wstring m_text;

    OnCharCallback     m_onChar;
    OnSubmitCallback   m_onSubmit;
    OnNavigateCallback m_onNavigate; // nav pulse + type haptic

    bool m_prevSouth = false;
    bool m_prevEast  = false;
    bool m_prevWest  = false;
    bool m_prevNorth = false;
    bool m_prevLB    = false;
    bool m_prevRB    = false;
    bool m_prevLS    = false;

    // =========================================================================
    // Right-stick proximity hover
    //
    // When ||RS|| > kRightStickDz the key whose screen-space centre is nearest
    // to (panelCentreX + rx * rsHalfW, panelCentreY + ry * rsHalfH) is selected.
    // Panel geometry is cached in Draw() and read in Update().
    // =========================================================================
    static constexpr float kRightStickDz = 0.20f;

    // Geometry cache (written in Draw const, read in Update)
    mutable float m_rsCentreX  = 0.0f;
    mutable float m_rsCentreY  = 0.0f;
    mutable float m_rsHalfW    = 0.0f;
    mutable float m_rsHalfH    = 0.0f;
    mutable float m_rsDpiScale = 1.0f;
    mutable float m_rsScreenW  = 0.0f;
    mutable float m_rsScreenH  = 0.0f;

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
