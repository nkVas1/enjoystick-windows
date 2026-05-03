#include <enjoystick/input/KeyboardMapper.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace enjoystick::input {

// ---------------------------------------------------------------------------
// Default bindings
// ---------------------------------------------------------------------------

static const std::pair<Button, KeyBinding> kDefaultBindings[] = {
    { Button::DPadUp,    { VK_UP,      false, false, false, true  } },
    { Button::DPadDown,  { VK_DOWN,    false, false, false, true  } },
    { Button::DPadLeft,  { VK_LEFT,    false, false, false, true  } },
    { Button::DPadRight, { VK_RIGHT,   false, false, false, true  } },
    { Button::South,     { VK_RETURN,  false, false, false, false } },
    { Button::East,      { VK_ESCAPE,  false, false, false, false } },
    { Button::North,     { VK_F5,      false, false, false, false } },
    { Button::West,      { VK_SPACE,   false, false, false, false } },
    { Button::LB,        { VK_TAB,     false, false, false, false } },
    { Button::RB,        { VK_TAB,     true,  false, false, false } },  // Shift+Tab
    { Button::Select,    { VK_LWIN,    false, false, false, true  } },
    { Button::LS,        { VK_F2,      false, false, false, false } },
    { Button::RS,        { VK_APPS,    false, false, false, false } },  // context menu
};

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

KeyboardMapper::KeyboardMapper() {
    ResetToDefaults();
}

void KeyboardMapper::ResetToDefaults() {
    m_bindings.clear();
    for (const auto& [btn, binding] : kDefaultBindings) {
        m_bindings[static_cast<uint32_t>(btn)] = binding;
    }
}

void KeyboardMapper::Bind(Button button, KeyBinding binding) {
    m_bindings[static_cast<uint32_t>(button)] = std::move(binding);
}

void KeyboardMapper::Unbind(Button button) {
    m_bindings.erase(static_cast<uint32_t>(button));
}

void KeyboardMapper::SetEnabled(bool enabled) noexcept { m_enabled = enabled; }
bool KeyboardMapper::IsEnabled() const noexcept { return m_enabled; }

// ---------------------------------------------------------------------------
// Update — detect rising/falling edges, inject keys
// ---------------------------------------------------------------------------

void KeyboardMapper::Update(const ControllerState& state) {
    if (!m_enabled) { m_prevButtons = state.buttons; return; }

    // Rising edge = button just pressed
    const uint32_t pressed  = static_cast<uint32_t>(state.buttonsDown);
    const uint32_t released = static_cast<uint32_t>(state.buttonsUp);

    for (const auto& [mask, binding] : m_bindings) {
        if (binding.vkCode == 0) continue;

        if (pressed  & mask) InjectKey(binding, /*down=*/true);
        if (released & mask) InjectKey(binding, /*down=*/false);
    }

    m_prevButtons = state.buttons;
}

// ---------------------------------------------------------------------------
// SendInput helpers
// ---------------------------------------------------------------------------

void KeyboardMapper::SendModifiers(const KeyBinding& binding, bool down) {
    auto sendMod = [&](uint16_t vk) {
        INPUT in    = {};
        in.type     = INPUT_KEYBOARD;
        in.ki.wVk   = vk;
        in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
        SendInput(1, &in, sizeof(INPUT));
    };
    if (binding.withShift) sendMod(VK_SHIFT);
    if (binding.withCtrl)  sendMod(VK_CONTROL);
    if (binding.withAlt)   sendMod(VK_MENU);
}

void KeyboardMapper::InjectKey(const KeyBinding& binding, bool down) {
    if (down) SendModifiers(binding, /*down=*/true);

    INPUT in      = {};
    in.type       = INPUT_KEYBOARD;
    in.ki.wVk     = binding.vkCode;
    in.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP)
                  | (binding.extended ? KEYEVENTF_EXTENDEDKEY : 0);
    SendInput(1, &in, sizeof(INPUT));

    if (!down) SendModifiers(binding, /*down=*/false);
}

} // namespace enjoystick::input
