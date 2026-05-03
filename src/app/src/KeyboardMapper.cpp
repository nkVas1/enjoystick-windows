#include <enjoystick/app/KeyboardMapper.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <array>

namespace enjoystick::app {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void SendVK(WORD vk, bool down) {
    INPUT input{};
    input.type       = INPUT_KEYBOARD;
    input.ki.wVk     = vk;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

static void SendVKWithMods(const ButtonMapping& m, bool down) {
    if (down) {
        if (m.withCtrl)  SendVK(VK_CONTROL, true);
        if (m.withShift) SendVK(VK_SHIFT,   true);
        if (m.withAlt)   SendVK(VK_MENU,    true);
        SendVK(m.virtualKey, true);
    } else {
        SendVK(m.virtualKey, false);
        if (m.withAlt)   SendVK(VK_MENU,    false);
        if (m.withShift) SendVK(VK_SHIFT,   false);
        if (m.withCtrl)  SendVK(VK_CONTROL, false);
    }
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

KeyboardMapper::KeyboardMapper() {
    ResetDefaults();
}

void KeyboardMapper::ResetDefaults() {
    m_map.clear();
    // D-pad → arrow keys (universal navigation)
    m_map[static_cast<uint32_t>(Button::DPadUp)]    = {VK_UP};
    m_map[static_cast<uint32_t>(Button::DPadDown)]  = {VK_DOWN};
    m_map[static_cast<uint32_t>(Button::DPadLeft)]  = {VK_LEFT};
    m_map[static_cast<uint32_t>(Button::DPadRight)] = {VK_RIGHT};
    // Face buttons
    m_map[static_cast<uint32_t>(Button::South)]     = {VK_RETURN};       // A / Cross  = Enter
    m_map[static_cast<uint32_t>(Button::East)]      = {VK_ESCAPE};       // B / Circle = Escape
    m_map[static_cast<uint32_t>(Button::North)]     = {VK_F5};           // Y / Triangle = F5 (refresh)
    m_map[static_cast<uint32_t>(Button::West)]      = {VK_BACK};         // X / Square = Backspace
    // Start / Select
    m_map[static_cast<uint32_t>(Button::Start)]     = {VK_LWIN};         // Start = Win key
    m_map[static_cast<uint32_t>(Button::Select)]    = {VK_TAB, false, true}; // Select = Alt+Tab
    // Shoulders → browser navigation
    m_map[static_cast<uint32_t>(Button::LB)]        = {VK_BROWSER_BACK};
    m_map[static_cast<uint32_t>(Button::RB)]        = {VK_BROWSER_FORWARD};
    // Stick clicks
    m_map[static_cast<uint32_t>(Button::LS)]        = {VK_LCONTROL};     // LS = Ctrl (multi-select)
    m_map[static_cast<uint32_t>(Button::RS)]        = {VK_APPS};         // RS = Context menu
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void KeyboardMapper::Update(const ControllerState& state) {
    if (!m_enabled) {
        m_prevButtons = state.buttons;
        return;
    }

    // Check each mapped button for rising / falling edges
    for (const auto& [btnVal, mapping] : m_map) {
        if (mapping.virtualKey == 0) continue;
        const auto btn = static_cast<Button>(btnVal);

        const bool nowDown  = HasButton(state.buttons,  btn);
        const bool prevDown = HasButton(m_prevButtons,  btn);

        if (nowDown && !prevDown)  SendVKWithMods(mapping, /*down=*/true);
        if (!nowDown && prevDown)  SendVKWithMods(mapping, /*down=*/false);
    }

    m_prevButtons = state.buttons;
}

void KeyboardMapper::SetMapping(Button btn, ButtonMapping mapping) {
    m_map[static_cast<uint32_t>(btn)] = std::move(mapping);
}

void KeyboardMapper::PressKey(const ButtonMapping& m) const   { SendVKWithMods(m, true);  }
void KeyboardMapper::ReleaseKey(const ButtonMapping& m) const { SendVKWithMods(m, false); }

} // namespace enjoystick::app
