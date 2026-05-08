#pragma once

// ---------------------------------------------------------------------------
// VoiceInputHUD  (v2)
//
// Redesigned voice-input overlay:
//   - 480×210 px panel (up from 340×120)
//   - Left: animated mic visualizer with state-coded colours
//     (muted = grey, listening = gold dim, recognizing = gold bright)
//     + spring-driven glow ring + mic level % label
//   - Right: status label, 16-segment VU bar with colour gradient
//     (gold→amber→orange-red at loud levels) + waveform animation
//     + partial-result text with scroll
//   - Bottom: exit/lang hint line
//   - Top: accent stripe whose colour changes with mic state
// ---------------------------------------------------------------------------

#include <enjoystick/overlay/Overlay_SpringAnim.hpp>
#include <enjoystick/voice/VoiceInput.hpp>

#include <string>

namespace enjoystick::overlay {

class VoiceInputHUD {
public:
    VoiceInputHUD() = default;

    void Open();
    void Close();
    [[nodiscard]] bool IsOpen() const noexcept;

    void SetVoiceState(const voice::VoiceInputState& vs) { m_vs = vs; }

    void Update(const voice::VoiceInputState& vs, float dt);

    void Draw(void* renderTargetPtr, void* dwriteFactoryPtr,
              float dpiScale, float screenW, float screenH) const;

    void SetLanguageLabel(const wchar_t* label) { m_langLabel = label ? label : L""; }

private:
    enum class State : uint8_t { Hidden, Opening, Visible, Closing };

    State  m_state           = State::Hidden;
    float  m_animT           = 0.0f;
    float  m_glowPhase       = 0.0f;
    float  m_wavePhase       = 0.0f;   // drives VU bar wave animation
    float  m_levelSmooth     = 0.0f;
    float  m_partialScrollX  = 0.0f;

    voice::VoiceInputState m_vs;
    std::wstring           m_langLabel = L"RU";

    mutable FloatSpring m_panelSpring;
    mutable FloatSpring m_micRingSpring;
    mutable FloatSpring m_levelBarSpring;

    static constexpr float kAnimSpeed  = 9.0f;
    static constexpr float kGlowHz     = 1.6f;
    static constexpr float kLevelDecay = 5.0f;
};

} // namespace enjoystick::overlay
