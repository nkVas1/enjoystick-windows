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
    // kScrollRyDz raised 0.40 → 0.65: the right stick must be pushed past
    // 65% of full deflection before scroll is triggered, preventing
    // accidental scroll from resting-position drift or brief brush.
    //
    // kStickFirst raised 0.70 → 1.00 s: deliberate hold required to tab.
    // kScrollFirst raised 0.65 → 0.90 s: deliberate hold required to scroll.
    // kScrollNext  raised 0.22 → 0.30 s: slower repeat cadence.
    static constexpr float kStickFirst  = 1.00f;   // was 0.70
    static constexpr float kStickNext   = 0.45f;   // was 0.35
    static constexpr float kScrollFirst = 0.90f;   // was 0.65
    static constexpr float kScrollNext  = 0.30f;   // was 0.22
    static constexpr float kScrollRyDz  = 0.65f;   // was 0.40  <-- key fix

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
