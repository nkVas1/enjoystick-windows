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
    // Single-step policy: a stick deflection held for up to 1.5 s must
    // produce exactly ONE navigation event.  Auto-repeat only begins after
    // kStickFirst / kScrollFirst seconds and then fires at the flat cadence.
    //
    // kStickFirst  = 1.50 s  — section-switch gate (was 1.00 s)
    // kStickNext   = 0.25 s  — flat repeat cadence after gate (was 0.45 s)
    // kScrollFirst = 1.50 s  — scroll-row gate, same policy (was 0.90 s)
    // kScrollNext  = 0.20 s  — flat cadence after gate (was 0.30 s)
    // kScrollRyDz  = 0.65    — right-stick scroll deadzone (unchanged)
    static constexpr float kStickFirst  = 1.50f;   // s before first section repeat
    static constexpr float kStickNext   = 0.25f;   // s between repeats (flat)
    static constexpr float kScrollFirst = 1.50f;   // s before first scroll repeat
    static constexpr float kScrollNext  = 0.20f;   // s between scroll repeats (flat)
    static constexpr float kScrollRyDz  = 0.65f;   // right-stick scroll deadzone

    float m_stickCooldown  = 0.0f;
    bool  m_stickActive    = false;

    bool  m_scrollDpadHeld  = false;
    float m_scrollDpadTimer = 0.0f;

    bool  m_scrollRyActive   = false;
    float m_scrollRyCooldown = 0.0f;

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
