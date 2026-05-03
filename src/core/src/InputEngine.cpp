#include <enjoystick/core/InputEngine.hpp>
#include "InputBackend_XInput.hpp"

namespace enjoystick::core {

///
/// InputEngine::Create — factory that selects the appropriate backend.
///
/// Currently only the XInput backend is implemented.  When the HID/RawInput
/// backend is added, this function will check config.backendPreference and
/// fall back to XInput if HID enumeration returns no devices.
///
std::unique_ptr<InputEngine> InputEngine::Create(Config config) {
    DeadzoneFilter dz;  // default ScaledRadial deadzone
    return std::make_unique<XInputBackend>(std::move(config), std::move(dz));
}

} // namespace enjoystick::core
