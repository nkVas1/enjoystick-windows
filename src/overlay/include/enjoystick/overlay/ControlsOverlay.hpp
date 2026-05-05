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

    // ---- Navigation timings (slower than before) ----------------------------
    static constexpr float kStickFirst  = 0.70f;
    static constexpr float kStickNext   = 0.35f;
    static constexpr float kScrollFirst = 0.65f;
    static constexpr float kScrollNext  = 0.22f;
    static constexpr float kScrollRyDz  = 0.40f;

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
