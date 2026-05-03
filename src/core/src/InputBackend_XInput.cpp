#include "InputBackend_XInput.hpp"
#include <enjoystick/shared/Types.hpp>
#include <spdlog/spdlog.h>
#include <Windows.h>
#include <Xinput.h>
#include <array>
#include <stdexcept>

namespace enjoystick::core {

namespace {

/// Map XInput XINPUT_STATE to our canonical ControllerState.
ControllerState MapXInputState(ControllerId id, const XINPUT_STATE& xi) noexcept {
    ControllerState s{};
    s.id           = id;
    s.backendType  = InputBackendType::XInput;

    const auto& gp = xi.Gamepad;

    // --- Buttons ---
    auto btn = [&](WORD mask, Button b) {
        if (gp.wButtons & mask) s.buttons.set(static_cast<size_t>(b));
    };
    btn(XINPUT_GAMEPAD_A,              Button::South);
    btn(XINPUT_GAMEPAD_B,              Button::East);
    btn(XINPUT_GAMEPAD_X,              Button::West);
    btn(XINPUT_GAMEPAD_Y,              Button::North);
    btn(XINPUT_GAMEPAD_LEFT_SHOULDER,  Button::L1);
    btn(XINPUT_GAMEPAD_RIGHT_SHOULDER, Button::R1);
    btn(XINPUT_GAMEPAD_LEFT_THUMB,     Button::L3);
    btn(XINPUT_GAMEPAD_RIGHT_THUMB,    Button::R3);
    btn(XINPUT_GAMEPAD_START,          Button::Start);
    btn(XINPUT_GAMEPAD_BACK,           Button::Select);
    btn(XINPUT_GAMEPAD_DPAD_UP,        Button::DpadUp);
    btn(XINPUT_GAMEPAD_DPAD_DOWN,      Button::DpadDown);
    btn(XINPUT_GAMEPAD_DPAD_LEFT,      Button::DpadLeft);
    btn(XINPUT_GAMEPAD_DPAD_RIGHT,     Button::DpadRight);

    // --- Analog sticks (normalize to [-1, 1]) ---
    constexpr float kMaxShort = 32767.0f;
    s.leftStick.x  =  static_cast<float>(gp.sThumbLX) / kMaxShort;
    s.leftStick.y  =  static_cast<float>(gp.sThumbLY) / kMaxShort;
    s.rightStick.x =  static_cast<float>(gp.sThumbRX) / kMaxShort;
    s.rightStick.y =  static_cast<float>(gp.sThumbRY) / kMaxShort;

    // --- Triggers (normalize to [0, 1]) ---
    constexpr float kMaxByte = 255.0f;
    s.leftTrigger  = static_cast<float>(gp.bLeftTrigger)  / kMaxByte;
    s.rightTrigger = static_cast<float>(gp.bRightTrigger) / kMaxByte;

    s.packetNumber = xi.dwPacketNumber;
    return s;
}

} // namespace

// XInputBackend implementation lives in InputBackend_XInput.hpp (internal)
// Full impl wired through InputEngine factory.

} // namespace enjoystick::core
