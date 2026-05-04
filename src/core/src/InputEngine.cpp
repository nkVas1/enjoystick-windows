#include <enjoystick/core/InputEngine.hpp>
#include "InputBackend_XInput.hpp"
#include "InputBackend_HID.hpp"

namespace enjoystick::core {

///
/// InputEngine::Create — backend selection strategy:
///
///   1. If preference == XInputOnly  →  always use XInputBackend.
///   2. If preference == HIDOnly     →  try HidBackend; throw if not found.
///   3. Default (Auto)               →  try HidBackend first (DualSense),
///                                      fall back to XInputBackend.
///
std::unique_ptr<InputEngine> InputEngine::Create(Config config) {
    DeadzoneFilter dz;  // default ScaledRadial deadzone

    const auto pref = config.backendPreference;

    if (pref != InputBackendPreference::XInputOnly) {
        auto hid = std::make_unique<HidBackend>(config, dz);
        if (hid->TryOpen()) {
            return hid;
        }
        // No DualSense found — fall through to XInput (unless HIDOnly)
        if (pref == InputBackendPreference::HIDOnly) {
            throw std::runtime_error(
                "EnjoyStick: HIDOnly backend requested but no DualSense device found");
        }
    }

    return std::make_unique<XInputBackend>(std::move(config), std::move(dz));
}

} // namespace enjoystick::core
