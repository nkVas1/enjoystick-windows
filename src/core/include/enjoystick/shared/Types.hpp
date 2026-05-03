#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace enjoystick {

// ─── Primitive math types ────────────────────────────────────────────────────

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    [[nodiscard]] constexpr float LengthSq() const noexcept { return x * x + y * y; }
};

struct Vec2i {
    int32_t x = 0;
    int32_t y = 0;
};

struct Rect {
    int32_t left   = 0;
    int32_t top    = 0;
    int32_t right  = 0;
    int32_t bottom = 0;

    [[nodiscard]] constexpr int32_t Width()  const noexcept { return right  - left; }
    [[nodiscard]] constexpr int32_t Height() const noexcept { return bottom - top;  }
};

// ─── Controller identity ─────────────────────────────────────────────────────

using ControllerId = uint8_t;
constexpr ControllerId kInvalidControllerId = 0xFF;

enum class ConnectionEvent : uint8_t { Connected, Disconnected };

enum class ControllerType : uint8_t {
    Unknown,
    Xbox,
    PlayStation,
    Generic,
};

struct ControllerInfo {
    ControllerId  id   = kInvalidControllerId;
    ControllerType type = ControllerType::Unknown;
    std::string   name;
};

// ─── Button bitmask ──────────────────────────────────────────────────────────

enum class Button : uint32_t {
    None        = 0,
    // Face
    South       = 1 << 0,   ///< A / Cross
    East        = 1 << 1,   ///< B / Circle
    West        = 1 << 2,   ///< X / Square
    North       = 1 << 3,   ///< Y / Triangle
    // Shoulders / Triggers (digital)
    LB          = 1 << 4,
    RB          = 1 << 5,
    LT_Click    = 1 << 6,
    RT_Click    = 1 << 7,
    // System
    Start       = 1 << 8,
    Select      = 1 << 9,
    Guide       = 1 << 10,  ///< PS / Xbox home button
    // Stick clicks
    LS          = 1 << 11,
    RS          = 1 << 12,
    // D-pad
    DPadUp      = 1 << 13,
    DPadDown    = 1 << 14,
    DPadLeft    = 1 << 15,
    DPadRight   = 1 << 16,
    // Touchpad (PS)
    Touchpad    = 1 << 17,
};

[[nodiscard]] constexpr Button operator|(Button a, Button b) noexcept {
    return static_cast<Button>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
[[nodiscard]] constexpr bool HasButton(Button state, Button mask) noexcept {
    return (static_cast<uint32_t>(state) & static_cast<uint32_t>(mask)) != 0;
}

// ─── Full controller state snapshot ──────────────────────────────────────────

struct ControllerState {
    ControllerId id          = kInvalidControllerId;
    Button       buttons     = Button::None;
    Button       buttonsDown = Button::None;  ///< Rising edge this frame
    Button       buttonsUp   = Button::None;  ///< Falling edge this frame
    Vec2         leftStick   = {};
    Vec2         rightStick  = {};
    float        leftTrigger  = 0.0f;  ///< [0, 1]
    float        rightTrigger = 0.0f;  ///< [0, 1]
    uint64_t     timestamp   = 0;      ///< QueryPerformanceCounter ticks
};

// ─── Haptics ─────────────────────────────────────────────────────────────────

struct RumbleParams {
    float    lowFreqMotor  = 0.0f;  ///< [0, 1]
    float    highFreqMotor = 0.0f;  ///< [0, 1]
    uint32_t durationMs    = 100;
};

// ─── Input backend preference ────────────────────────────────────────────────

enum class InputBackendPreference : uint8_t {
    Auto,       ///< Prefer RawInput/HID, fall back to XInput
    XInputOnly,
    HIDOnly,
};

// ─── Callback lifetime handle ────────────────────────────────────────────────

/// RAII handle: destroying this unregisters the associated callback.
class CallbackHandle {
public:
    using Deleter = std::function<void()>;

    CallbackHandle() = default;
    explicit CallbackHandle(Deleter deleter) : m_deleter(std::move(deleter)) {}

    CallbackHandle(const CallbackHandle&)            = delete;
    CallbackHandle& operator=(const CallbackHandle&) = delete;

    CallbackHandle(CallbackHandle&& other) noexcept
        : m_deleter(std::move(other.m_deleter)) { other.m_deleter = nullptr; }

    CallbackHandle& operator=(CallbackHandle&& other) noexcept {
        Reset();
        m_deleter = std::move(other.m_deleter);
        other.m_deleter = nullptr;
        return *this;
    }

    ~CallbackHandle() { Reset(); }
    void Reset() { if (m_deleter) { m_deleter(); m_deleter = nullptr; } }

private:
    Deleter m_deleter;
};

} // namespace enjoystick
