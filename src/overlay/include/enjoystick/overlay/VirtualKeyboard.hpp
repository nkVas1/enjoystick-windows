#pragma once

#include <enjoystick/shared/Types.hpp>
#include <enjoystick/overlay/Overlay_SpringAnim.hpp>

#include <functional>
#include <string>
#include <vector>

namespace enjoystick::overlay {

// ---------------------------------------------------------------------------
// VirtualKeyboard
//
// Gamepad-driven on-screen QWERTY keyboard.
//
// Controls (standard layout):
//   Left stick  - move cursor (spring-snapping between keys)
//   South (A)   - type highlighted key
//   West  (X)   - backspace
//   East  (B)   - close (cancel)
//   North (Y)   - confirm / submit text
//   LB / RB     - switch to symbol layer / back
//   L3 (click)  - toggle Caps Lock
//
// The keyboard calls OnChar for every character typed and OnSubmit when
// the user confirms. The caller owns the accumulated string and may pass
// a seed string via Open().
//
// Animation model (v4):
//   Panel slide-in: FloatSpring (stiffness=320, damping=24), 0->1
//   Cursor glow:    FloatSpring x/y (stiffness=480, damping=26)
//   Key scale pop:  FloatSpring (stiffness=600, damping=28), 1.18->1
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

    /// Returns a short uppercase label for the current layer/modifier state:
    /// "SYM", "CAPS", or "ALPHA".
    [[nodiscard]] const wchar_t* GetLayerName() const noexcept {
        if (m_layer == Layer::Sym)  return L"SYM";
        if (m_caps)                 return L"CAPS";
        return L"ALPHA";
    }

private:
    // ---- Key grid -----------------------------------------------------------
    struct Key {
        std::wstring label;         // normal label
        std::wstring shiftLabel;    // Shift / Caps label
        std::wstring symLabel;      // symbol layer label
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

    // navigation
    float  m_stickCooldown = 0.0f;
    static constexpr float kStickRepeatFirst = 0.35f;
    static constexpr float kStickRepeatNext  = 0.10f;
    bool   m_stickActive = false;

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
