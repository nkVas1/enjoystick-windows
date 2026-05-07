#pragma once

#include <enjoystick/shared/Types.hpp>
#include <enjoystick/overlay/Overlay_SpringAnim.hpp>

#include <functional>
#include <string>
#include <vector>

namespace enjoystick::overlay {

enum class HapticType : uint8_t;

class ControlsOverlay {
public:
    using OnHapticCallback = std::function<void(HapticType)>;

    ControlsOverlay() = default;
    ~ControlsOverlay() = default;

    void Open();
    void Close();
    [[nodiscard]] bool IsOpen() const noexcept;

    void SetOnHaptic(OnHapticCallback cb) { m_onHaptic = std::move(cb); }

    void Update(const ControllerState& state, float deltaSeconds);
    void Draw  (void* renderTargetPtr,
                void* dwriteFactoryPtr,
                float dpiScale,
                float screenW,
                float screenH) const;

private:
    struct Binding { std::wstring keys; std::wstring action; };
    struct Section  { std::wstring icon; std::wstring title; std::vector<Binding> bindings; };

    enum class State : uint8_t { Hidden, Opening, Visible, Closing };

    void BuildSections();

    std::vector<Section> m_sections;
    int32_t m_sectionIdx   = 0;
    int32_t m_scrollOffset = 0;
    State   m_state        = State::Hidden;

    // ---- Navigation timings -------------------------------------------------
    // HYSTERESIS (same fix as VirtualKeyboard / SettingsMenu, 2026-05-07):
    //   kStickDeadzone / kScrollRyDz  = activate thresholds.
    //   kStickRelease  / kScrollRyRelease = deactivate thresholds.
    //   The stick must retreat below the release threshold before a new
    //   first-step can fire; intermediate values are silently ignored.
    //
    // kStickFirst  = 1.00 s  — section-switch gate (was 1.50 s)
    // kStickNext   = 0.25 s  — flat repeat cadence after gate
    // kScrollFirst = 1.00 s  — scroll-row gate (was 1.50 s)
    // kScrollNext  = 0.20 s  — flat cadence after gate
    static constexpr float kStickDeadzone   = 0.50f;   // activate threshold (left stick X)
    static constexpr float kStickRelease    = 0.28f;   // deactivate threshold (hysteresis)
    static constexpr float kStickFirst      = 1.00f;   // s before first section repeat
    static constexpr float kStickNext       = 0.25f;   // s between repeats (flat)
    static constexpr float kScrollRyDz      = 0.65f;   // activate threshold (right stick Y)
    static constexpr float kScrollRyRelease = 0.30f;   // deactivate threshold (hysteresis)
    static constexpr float kScrollFirst     = 1.00f;   // s before first scroll repeat
    static constexpr float kScrollNext      = 0.20f;   // s between scroll repeats (flat)

    // Left Stick X (section switch) — hysteresis pair
    float m_stickCooldown    = 0.0f;
    bool  m_stickActive      = false;
    bool  m_stickWasActive   = false;  // true while |lx| >= kStickDeadzone

    // DPad scroll
    bool  m_scrollDpadHeld  = false;
    float m_scrollDpadTimer = 0.0f;

    // Right Stick Y (scroll) — hysteresis pair
    bool  m_scrollRyActive    = false;
    bool  m_scrollRyWasActive = false; // true while |ry| >= kScrollRyDz
    float m_scrollRyCooldown  = 0.0f;

    mutable FloatSpring m_panelSpring;
    mutable FloatSpring m_tabSpring;

    Button m_openMask = Button::None;

    bool m_prevEast   = false;
    bool m_prevSouth  = false;
    bool m_prevDLeft  = false;
    bool m_prevDRight = false;
    bool m_prevDUp    = false;
    bool m_prevDDown  = false;

    OnHapticCallback m_onHaptic;
};

} // namespace enjoystick::overlay
