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
//   DPad Left / Right        — switch between sections
//   DPad Up   / Down         — scroll binding list
//   Left Stick X             — switch between sections
//   Right Stick Y            — scroll binding list (analogue)
//   East  (B/Circle)         — close
//   South (A/Cross)          — close
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
    mutable FloatSpring m_tabSpring;

    // Section-switch stick debounce
    float   m_stickCooldown  = 0.0f;
    bool    m_stickActive    = false;
    static constexpr float kStickFirst = 0.55f;
    static constexpr float kStickNext  = 0.25f;

    // Scroll
    int32_t m_scrollOffset   = 0;
    bool    m_scrollDpadHeld = false;
    float   m_scrollDpadTimer= 0.0f;
    bool    m_scrollRyActive = false;
    float   m_scrollRyCooldown= 0.0f;
    static constexpr float kScrollFirst = 0.40f;
    static constexpr float kScrollNext  = 0.18f;
    static constexpr float kScrollRyDz  = 0.35f;

    bool m_prevEast   = false;
    bool m_prevSouth  = false;
    bool m_prevDLeft  = false;
    bool m_prevDRight = false;
    bool m_prevDUp    = false;
    bool m_prevDDown  = false;
};

} // namespace enjoystick::overlay
