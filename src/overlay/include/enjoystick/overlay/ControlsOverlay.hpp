#pragma once

#include <enjoystick/shared/Types.hpp>
#include <enjoystick/overlay/Overlay_SpringAnim.hpp>

#include <string>
#include <vector>

namespace enjoystick::overlay {

// ---------------------------------------------------------------------------
// ControlsOverlay
//
// Full-screen reference panel listing every gamepad command and combo.
// Opened from the radial menu “Controls” sector.
//
// Controls (inside overlay):
//   DPad Left / Right   — switch between sections
//   Left Stick X        — switch between sections
//   East  (B/Circle)    — close
//   South (A/Cross)     — close
// ---------------------------------------------------------------------------
class ControlsOverlay {
public:
    ControlsOverlay()  = default;
    ~ControlsOverlay() = default;

    void Open();
    void Close();
    [[nodiscard]] bool IsOpen() const noexcept;

    void Update(const ControllerState& state, float deltaSeconds);
    void Draw  (void* renderTargetPtr,
                void* dwriteFactoryPtr,
                float dpiScale,
                float screenW,
                float screenH) const;

private:
    struct Binding {
        std::wstring keys;   // e.g. L"\u25CF" or L"\u25CF + \u25A0"
        std::wstring action; // human-readable description
    };
    struct Section {
        std::wstring          icon;
        std::wstring          title;
        std::vector<Binding>  bindings;
    };

    enum class State : uint8_t { Hidden, Opening, Visible, Closing };

    void BuildSections();

    std::vector<Section>  m_sections;
    int32_t               m_sectionIdx  = 0;

    State   m_state      = State::Hidden;
    mutable FloatSpring m_panelSpring;

    // Section tab slide spring (tracks section index as float)
    mutable FloatSpring m_tabSpring;

    // Stick nav debounce
    float   m_stickCooldown  = 0.0f;
    bool    m_stickActive    = false;
    static constexpr float kStickFirst = 0.55f;
    static constexpr float kStickNext  = 0.25f;

    bool m_prevEast  = false;
    bool m_prevSouth = false;
    bool m_prevDLeft = false;
    bool m_prevDRight= false;
};

} // namespace enjoystick::overlay
