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
//   South (A/Cross)    - type highlighted key
//   West  (X/Square)   - backspace
//   East  (B/Circle)   - close / cancel
//   North (Y/Tri)      - confirm / submit text
//   LB                 - cycle layer: Alpha -> Cyr -> Sym -> Alpha
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
        std::wstring label;      // shown in Alpha layer (lower)
        std::wstring shiftLabel; // shown when Shift/Caps in Alpha layer
        std::wstring symLabel;   // shown in Sym layer
        std::wstring cyrLabel;   // shown in Cyr layer (lower)
        std::wstring cyrShift;   // shown in Cyr layer + caps
        float        widthMul  = 1.0f;
        bool         isSpecial = false;
    };

    enum class State : uint8_t { Hidden, Opening, Visible, Closing };

    void BuildLayout();

    // grid
    std::vector<std::vector<Key>> m_rows;

    // cursor (logical grid)
    int32_t m_row = 0;
    int32_t m_col = 0;

    // --------------------------------------------------------------------------
    // Stick navigation timing  (250 Hz poll = 4 ms/frame)
    // kStickRepeatFirst: wait this long before auto-repeat kicks in
    //   -- long enough that a deliberate single-push won't accidentally
    //      move two keys, but short enough to feel snappy.
    // kStickRepeatNext:  interval once auto-repeat is active
    // kSnapDeadzone:     strong magnetic centre; stick must be pushed
    //   confidently to leave deadzone
    // --------------------------------------------------------------------------
    float  m_stickCooldown = 0.0f;
    static constexpr float kStickRepeatFirst = 0.55f;
    static constexpr float kStickRepeatNext  = 0.16f;
    static constexpr float kSnapDeadzone     = 0.62f;
    bool   m_stickActive = false;

    // --------------------------------------------------------------------------
    // DPad navigation timing
    // kDPadFirst: first-repeat delay (single step on tap)
    // kDPadNext:  auto-repeat while held (snappy navigation)
    // --------------------------------------------------------------------------
    static constexpr float kDPadFirst = 0.38f;
    static constexpr float kDPadNext  = 0.13f;
    bool  m_dpadHeld      = false;
    float m_dpadTimer     = 0.0f;
    int32_t m_dpadDirRow  = 0;
    int32_t m_dpadDirCol  = 0;

    // --------------------------------------------------------------------------
    // Type debounce: South button press is ignored until this timer expires.
    // Without it a single 4-ms physical press fires on multiple 250-Hz frames.
    // --------------------------------------------------------------------------
    static constexpr float kTypeDebounceMs = 180.0f;
    float m_typeDebounce = 0.0f;  // counts down in ms; 0 = ready to type

    // state
    State  m_state     = State::Hidden;
    float  m_glowPhase = 0.0f;
    Layer  m_layer     = Layer::Alpha;
    bool   m_shift     = false;
    bool   m_caps      = false;

    // Spring animations
    mutable FloatSpring m_panelSpring;
    mutable FloatSpring m_cursorSpringX;
    mutable FloatSpring m_cursorSpringY;
    mutable FloatSpring m_cursorScaleSpring;

    // accumulated text
    std::wstring m_text;

    // callbacks
    OnCharCallback   m_onChar;
    OnSubmitCallback m_onSubmit;

    // button edge detection
    bool m_prevSouth = false;
    bool m_prevEast  = false;
    bool m_prevWest  = false;
    bool m_prevNorth = false;
    bool m_prevLB    = false;
    bool m_prevRB    = false;
    bool m_prevLS    = false;

    // helpers
    [[nodiscard]] const Key* CurrentKey() const noexcept;
    [[nodiscard]] std::wstring KeyDisplay(const Key& k) const;
    void TypeKey(const Key& k);
    void NavigateTo(int32_t row, int32_t col);
    [[nodiscard]] int32_t RowKeyCount(int32_t row) const noexcept;
    [[nodiscard]] Vec2    KeyCentrePixel(int32_t row, int32_t col,
                                         float dpiScale,
                                         float screenW, float screenH) const noexcept;
};

} // namespace enjoystick::overlay
