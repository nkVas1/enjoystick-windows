#pragma once

// ---------------------------------------------------------------------------
// VoiceInputHUD
//
// Overlay component that draws the voice-input indicator:
//   - Centered animated microphone icon
//   - Language badge (RU / EN)
//   - Live partial-result text (scrolls if too wide)
//   - VU-level bar (audio level animation)
//   - Pulsing glow ring driven by FloatSpring
//
// Lifecycle follows the standard overlay pattern:
//   Open()  -- animate in
//   Close() -- animate out
//   Update(dt) -- advance internal state
//   Draw(rt, dwrite, dpiScale, w, h) -- render
// ---------------------------------------------------------------------------

#include <enjoystick/overlay/Overlay_SpringAnim.hpp>
#include <enjoystick/voice/VoiceInput.hpp>

#include <string>

namespace enjoystick::overlay {

class VoiceInputHUD {
public:
    VoiceInputHUD() = default;

    void Open();                // animate in
    void Close();               // animate out
    [[nodiscard]] bool IsOpen() const noexcept;

    // Called every render frame; vs = latest snapshot from VoiceInput::GetState()
    void Update(const voice::VoiceInputState& vs, float dt);

    void Draw(void* renderTargetPtr, void* dwriteFactoryPtr,
              float dpiScale, float screenW, float screenH) const;

    // Language badge text displayed in the HUD (e.g. L"RU" / L"EN")
    void SetLanguageLabel(const wchar_t* label) { m_langLabel = label ? label : L""; }

private:
    enum class State : uint8_t { Hidden, Opening, Visible, Closing };

    State  m_state        = State::Hidden;
    float  m_animT        = 0.0f;   // 0..1 panel open/close
    float  m_glowPhase    = 0.0f;   // drives pulsing ring
    float  m_levelSmooth  = 0.0f;   // smoothed audio level
    float  m_partialScrollX = 0.0f; // horizontal scroll for long partial text

    voice::VoiceInputState m_vs;    // latest state snapshot
    std::wstring           m_langLabel = L"RU";

    // Springs
    mutable FloatSpring m_panelSpring;      // panel scale pop
    mutable FloatSpring m_micRingSpring;    // glow ring radius
    mutable FloatSpring m_levelBarSpring;   // VU bar height

    static constexpr float kAnimSpeed  = 9.0f;   // open/close speed
    static constexpr float kGlowHz     = 1.8f;   // idle pulse frequency
    static constexpr float kLevelDecay = 6.0f;   // VU bar decay rate
};

} // namespace enjoystick::overlay
