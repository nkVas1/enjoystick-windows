#pragma once

#include <enjoystick/shared/Types.hpp>
#include <chrono>
#include <memory>

namespace enjoystick::core {

/// Predefined haptic patterns inspired by PlayStation DualSense haptic design.
enum class HapticPattern : uint8_t {
    Click,          ///< Short sharp click (UI confirm)
    SoftBump,       ///< Gentle bump (navigation step)
    DoubleTap,      ///< Two-pulse confirmation
    Error,          ///< Triple short pulses (error)
    LongPress,      ///< Sustained low-frequency rumble
    Custom,         ///< Defined by RumbleParams directly
};

///
/// HapticsEngine — abstracts controller vibration/haptics.
/// Supports both XInput (motor rumble) and SDL3 (richer haptic effects).
///
class HapticsEngine {
public:
    static std::unique_ptr<HapticsEngine> Create();
    virtual ~HapticsEngine() = default;

    /// Play a predefined haptic pattern on a controller.
    virtual void Play(ControllerId id, HapticPattern pattern, float intensity = 1.0f) = 0;

    /// Enqueue a raw rumble command.
    virtual void Rumble(ControllerId id, RumbleParams params) = 0;

    /// Cancel any ongoing haptic effect on a controller.
    virtual void Cancel(ControllerId id) = 0;

    /// Set global haptic master volume (0.0 = silent, 1.0 = full).
    virtual void SetMasterIntensity(float intensity) = 0;
};

} // namespace enjoystick::core
