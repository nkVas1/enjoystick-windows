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
//   South (A/Cross)    - type highlighted key  [hold for accelerating repeat]
//   West  (X/Square)   - backspace             [hold for accelerating repeat]
//   East  (B/Circle)   - close / cancel
//   North (Y/Tri)      - confirm / submit text
//   LB                 - cycle layer: Alpha -> Cyr -> Sym -> Alpha  (debounced)
//   RB                 - return to Alpha layer
//   L3 (click)         - toggle Caps Lock
//
// Layers:
//   ALPHA  - Latin lowercase / uppercase
//   CYR    - Russian (Cyrillic) layout
//   SYM    - Symbols and punctuation
// ---------------------------------------------------------------------------

class VirtualKeyboard {
public:
    using OnCharCallback   = std::function<void(wchar_t ch)>;
    using OnSubmitCallback = std::function<void(const std::wstring& text)>;

    enum class Layer : uint8_t { Alpha, Cyr, Sym };

    VirtualKeyboard() = default;
    ~VirtualKeyboard() = default;

    // ---- Lifecycle ----------------------------------------------------------
    void Open (const std::wstring& seed = L"");
    void Close();
    [[nodiscard]] bool IsOpen() const noexcept;

    // ---- Callbacks ----------------------------------------------------------
    void SetOnChar  (OnCharCallback   cb) { m_onChar   = std::move(cb); }
    void SetOnSubmit(OnSubmitCallback cb) { m_onSubmit = std::move(cb); }

    // ---- Haptic callback (set by Application to fire rumble) ----------------
    using HapticCallback = std::function<void(float lowFreq, float hiFreq, int durationMs)>;
    void SetOnHaptic(HapticCallback cb) { m_onHaptic = std::move(cb); }

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
    // Left-stick / DPad navigation timing
    //
    // Phase 1 — first step: kStickRepeatFirst (very long — single flick = 1 move)
    // Phase 2 — slow repeat: kStickRepeatNext  (springy feel)
    // Phase 3 — fast repeat: kStickRepeatFast  (only after kStickRepeatAccelStart
    //            seconds of continuous hold; blends exponentially)
    // -------------------------------------------------------------------------
    float  m_stickCooldown    = 0.0f;
    float  m_stickHoldTime    = 0.0f;  // accumulated hold duration for accel
    static constexpr float kStickRepeatFirst     = 1.05f;  // single-flick safety window
    static constexpr float kStickRepeatNext      = 0.22f;  // base auto-repeat
    static constexpr float kStickRepeatFast      = 0.055f; // max speed after accel
    static constexpr float kStickRepeatAccelStart= 1.40f;  // hold this long to start speeding up
    static constexpr float kStickRepeatAccelRange= 0.90f;  // blend range
    static constexpr float kSnapDeadzone         = 0.78f;  // stronger centre magnet
    bool   m_stickActive = false;

    static constexpr float kDPadFirst = 0.70f;  // initial delay before DPad repeat
    static constexpr float kDPadNext  = 0.19f;  // DPad repeat interval
    bool    m_dpadHeld    = false;
    float   m_dpadTimer   = 0.0f;
    int32_t m_dpadDirRow  = 0;
    int32_t m_dpadDirCol  = 0;

    // -------------------------------------------------------------------------
    // Hold-to-repeat for South (type key)
    //
    // kTypeFirstMs      – delay before first auto-repeat fires
    // kTypeRepeatSlowMs – initial repeat interval
    // kTypeRepeatFastMs – minimum repeat interval after kTypeAccelStart of hold
    // kTypeAccelStart   – seconds of hold before interval starts shrinking
    // kTypeAccelRange   – blend range to reach kTypeRepeatFastMs
    // -------------------------------------------------------------------------
    static constexpr float kTypeFirstMs      = 320.0f;
    static constexpr float kTypeRepeatSlowMs = 200.0f;
    static constexpr float kTypeRepeatFastMs =  60.0f;
    static constexpr float kTypeAccelStart   =   1.2f;
    static constexpr float kTypeAccelRange   =   0.8f;
    bool  m_southHeld       = false;
    float m_southHoldTime   = 0.0f;   // accumulated hold duration
    float m_southRepeatTimer= 0.0f;   // counts down to next fire

    // -------------------------------------------------------------------------
    // Hold-to-repeat for West (backspace)
    // -------------------------------------------------------------------------
    static constexpr float kBsFirstMs      = 360.0f;
    static constexpr float kBsRepeatSlowMs = 220.0f;
    static constexpr float kBsRepeatFastMs =  55.0f;
    static constexpr float kBsAccelStart   =   0.9f;
    static constexpr float kBsAccelRange   =   0.7f;
    bool  m_westHeld        = false;
    float m_westHoldTime    = 0.0f;
    float m_westRepeatTimer = 0.0f;

    // -------------------------------------------------------------------------
    // Simple one-shot debounce for layer switch (LB)
    // -------------------------------------------------------------------------
    static constexpr float kLbDebounceMs    = 320.0f;
    float m_lbDebounce   = 0.0f;

    State  m_state     = State::Hidden;
    float  m_glowPhase = 0.0f;
    Layer  m_layer     = Layer::Alpha;
    bool   m_shift     = false;
    bool   m_caps      = false;

    mutable FloatSpring m_panelSpring;
    mutable FloatSpring m_cursorSpringX;
    mutable FloatSpring m_cursorSpringY;
    // Trail spring: heavily damped / low-stiffness so it lags the cursor
    // visibly, creating the liquid 'ink blob' effect between keys.
    mutable FloatSpring m_trailSpringX;
    mutable FloatSpring m_trailSpringY;
    mutable FloatSpring m_cursorScaleSpring;

    // Trail blob alpha: set to 1 on each navigation step, decays to 0.
    mutable float m_trailAlpha = 0.0f;
    static constexpr float kTrailDecayRate = 3.8f;  // per second

    std::wstring m_text;

    OnCharCallback   m_onChar;
    OnSubmitCallback m_onSubmit;
    HapticCallback   m_onHaptic;

    bool m_prevSouth = false;
    bool m_prevEast  = false;
    bool m_prevWest  = false;
    bool m_prevNorth = false;
    bool m_prevLB    = false;
    bool m_prevRB    = false;
    bool m_prevLS    = false;

    // ---- Right-stick proximity hover ----------------------------------------
    // When the right stick is deflected beyond kRStickDz the key whose centre
    // pixel is nearest to the stick-projected position is selected instead of
    // using d-pad / left-stick discrete navigation.
    static constexpr float kRStickDz    = 0.28f;  // deflection threshold
    static constexpr float kRStickRange = 420.0f; // half-range in logical pixels
    bool    m_rStickActive = false;
    float   m_rStickX      = 0.0f;  // smoothed projected X
    float   m_rStickY      = 0.0f;  // smoothed projected Y

    // Cached screen geometry (set during Draw, read during Update for proximity)
    mutable float m_cachedScreenW  = 0.0f;
    mutable float m_cachedScreenH  = 0.0f;
    mutable float m_cachedDpiScale = 1.0f;
    mutable float m_cachedPanelCX  = 0.0f;  // panel centre X
    mutable float m_cachedPanelCY  = 0.0f;  // panel centre Y

    [[nodiscard]] const Key* CurrentKey() const noexcept;
    [[nodiscard]] std::wstring KeyDisplay(const Key& k) const;
    // Returns true if the key was a backspace action
    bool TypeKey(const Key& k);
    void NavigateTo(int32_t row, int32_t col);
    [[nodiscard]] int32_t RowKeyCount(int32_t row) const noexcept;
    [[nodiscard]] Vec2    KeyCentrePixel(int32_t row, int32_t col,
                                         float dpiScale,
                                         float screenW, float screenH) const noexcept;
};

} // namespace enjoystick::overlay
