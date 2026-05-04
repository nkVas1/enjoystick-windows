// WIN32_LEAN_AND_MEAN and NOMINMAX are injected by CMake for this module.
#include <enjoystick/input/KeyboardMapper.hpp>

#include <Windows.h>

#include <algorithm>
#include <vector>

namespace enjoystick::input {

// ---------------------------------------------------------------------------
// Default bindings
//
// Intentionally omitted:
//   Button::Select  — reserved for Application-level mode toggle
//   Button::Guide   — reserved for Application-level radial menu
//   Button::LB      — part of LB+RB chord for mode toggle
//   Button::RB      — part of LB+RB chord for mode toggle
// ---------------------------------------------------------------------------

static constexpr struct { Button btn; KeyBinding binding; } kDefaults[] = {
    { Button::DPadUp,    { VK_UP,     false, false, false, true  } },
    { Button::DPadDown,  { VK_DOWN,   false, false, false, true  } },
    { Button::DPadLeft,  { VK_LEFT,   false, false, false, true  } },
    { Button::DPadRight, { VK_RIGHT,  false, false, false, true  } },
    { Button::South,     { VK_RETURN, false, false, false, false } },
    { Button::East,      { VK_ESCAPE, false, false, false, false } },
    { Button::North,     { VK_F5,     false, false, false, false } },
    { Button::West,      { VK_SPACE,  false, false, false, false } },
    { Button::Start,     { VK_ESCAPE, false, false, false, false } },
    { Button::LS,        { VK_F2,     false, false, false, false } },
    { Button::RS,        { VK_APPS,   false, false, false, false } },
};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

KeyboardMapper::KeyboardMapper() {
    ResetToDefaults();
}

void KeyboardMapper::ResetToDefaults() {
    m_bindings.clear();
    for (const auto& d : kDefaults) {
        m_bindings[static_cast<uint32_t>(d.btn)] = d.binding;
    }
    m_prevButtons = Button::None;
}

void KeyboardMapper::Bind(Button button, KeyBinding binding) {
    m_bindings[static_cast<uint32_t>(button)] = binding;
}

void KeyboardMapper::Unbind(Button button) {
    m_bindings.erase(static_cast<uint32_t>(button));
}

void KeyboardMapper::SetEnabled(bool enabled) noexcept {
    m_enabled = enabled;
    if (!enabled) {
        // Release any currently held keys to avoid stuck keys
        const uint32_t held = static_cast<uint32_t>(m_prevButtons);
        for (const auto& [mask, binding] : m_bindings) {
            if (held & mask) InjectKey(binding, false);
        }
        m_prevButtons = Button::None;
    }
}

bool KeyboardMapper::IsEnabled() const noexcept { return m_enabled; }

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void KeyboardMapper::Update(const ControllerState& state) {
    if (!m_enabled) return;

    for (const auto& [mask, binding] : m_bindings) {
        if (binding.vkCode == 0) continue;

        const Button btn     = static_cast<Button>(mask);
        const bool   wasDown = HasButton(m_prevButtons, btn);
        const bool   isDown  = HasButton(state.buttons,  btn);

        if (isDown  && !wasDown) InjectKey(binding, true);
        if (!isDown && wasDown)  InjectKey(binding, false);
    }

    m_prevButtons = state.buttons;
}

// ---------------------------------------------------------------------------
// Private key injection
// ---------------------------------------------------------------------------

void KeyboardMapper::InjectKey(const KeyBinding& binding, bool down) {
    SendModifiers(binding, down);

    INPUT inp{};
    inp.type       = INPUT_KEYBOARD;
    inp.ki.wVk     = binding.vkCode;
    inp.ki.dwFlags = (binding.extended ? KEYEVENTF_EXTENDEDKEY : 0) |
                     (down             ? 0                      : KEYEVENTF_KEYUP);
    SendInput(1, &inp, sizeof(INPUT));

    // Release modifiers after key-up
    if (!down) SendModifiers(binding, false);
}

void KeyboardMapper::SendModifiers(const KeyBinding& binding, bool down) {
    const DWORD flags = down ? 0 : KEYEVENTF_KEYUP;
    auto send = [&](WORD vk) {
        INPUT inp{};
        inp.type       = INPUT_KEYBOARD;
        inp.ki.wVk     = vk;
        inp.ki.dwFlags = flags;
        SendInput(1, &inp, sizeof(INPUT));
    };
    if (binding.withCtrl)  send(VK_CONTROL);
    if (binding.withShift) send(VK_SHIFT);
    if (binding.withAlt)   send(VK_MENU);
}

} // namespace enjoystick::input
