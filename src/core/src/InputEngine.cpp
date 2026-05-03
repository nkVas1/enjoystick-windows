#include <enjoystick/core/InputEngine.hpp>
#include "InputBackend_XInput.hpp"

namespace enjoystick::core {

std::unique_ptr<InputEngine> InputEngine::Create(Config config) {
    auto dz = DeadzoneFilter{};
    return std::make_unique<XInputBackend>(config, std::move(dz));
}

} // namespace enjoystick::core
