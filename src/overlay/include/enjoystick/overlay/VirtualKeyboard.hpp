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
//   LB                 - toggle symbol layer
//   RB                 - return to alpha layer
//   L3 (click)         - toggle Caps Lock
//
// UX goals:
//   - Large, clear panel at bottom of screen with big readable keys
//   - Snap-to-key magnetic cursor (spring-interpolated, never drifts)
//   - DPad navigation for precise single-step movement
//   - Per-key press pop animation (scale bounce via FloatSpring)
//   - Thin focus ring matching key shape instead of heavy glow blob
//   - Direct SendInput per-char so text lands in the focused field
// ---------------------------------------------------------------------------

class VirtualKeyboard {
public:
    using OnCharCallback   = std::function<void(wchar_t ch)>;
    using OnSubmitCallback = std::function<void(const std::wstring& text)>;

    enum class Layer : uint8_t { Alpha, Sym };

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
        if (m_layer == Layer::Sym)  return L"SYM";
        if (m_caps)                 return L"CAPS";
        return L"ALPHA";
    }

private:
    // ---- Key grid -----------------------------------------------------------
    struct Key {
        std::wstring label;
        std::wstring shiftLabel;
        std::wstring symLabel;
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
    // Stick navigation timing
    // kStickRepeatFirst: first move fires after this delay — prevents drift
    // kStickRepeatNext:  comfortable auto-repeat after first move
    // kSnapDeadzone:     high threshold creates strong "centre magnet" feel
    // --------------------------------------------------------------------------
    float  m_stickCooldown = 0.0f;
    static constexpr float kStickRepeatFirst = 0.50f;  // was 0.80
    static constexpr float kStickRepeatNext  = 0.14f;  // was 0.22
    static constexpr float kSnapDeadzone     = 0.62f;  // was 0.55
    bool   m_stickActive = false;

    // --------------------------------------------------------------------------
    // DPad navigation timing
    // kDPadFirst: initial delay before auto-repeat kicks in
    // kDPadNext:  auto-repeat interval (snappy, not runaway)
    // --------------------------------------------------------------------------
    static constexpr float kDPadFirst = 0.35f;  // was 0.50
    static constexpr float kDPadNext  = 0.12f;  // was 0.20
    bool  m_dpadHeld      = false;
    float m_dpadTimer     = 0.0f;
    int32_t m_dpadDirRow  = 0;
    int32_t m_dpadDirCol  = 0;

    // state
    State  m_state     = State::Hidden;
    float  m_glowPhase = 0.0f;   // re-used for cursor blink only
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
