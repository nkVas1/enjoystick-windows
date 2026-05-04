// WIN32_LEAN_AND_MEAN and NOMINMAX injected by CMake.
#include <Windows.h>

#include "enjoystick/input/KeyboardMapper.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace enjoystick::input {

// ---------------------------------------------------------------------------
// Binding management
// ---------------------------------------------------------------------------

void KeyboardMapper::SetBindings(std::vector<KeyBinding> bindings) {
    m_bindings = std::move(bindings);
    SortBySpecificity();
    m_prevButtons = 0;
    m_activeChord = 0;
}

void KeyboardMapper::AddBinding(KeyBinding binding) {
    m_bindings.push_back(std::move(binding));
    SortBySpecificity();
}

void KeyboardMapper::ClearBindings() {
    m_bindings.clear();
    m_prevButtons = 0;
    m_activeChord = 0;
}

const std::vector<KeyBinding>& KeyboardMapper::GetBindings() const noexcept {
    return m_bindings;
}

void KeyboardMapper::SetEnabled(bool enabled) noexcept {
    m_enabled = enabled;
    if (!enabled) {
        m_prevButtons = 0;
        m_activeChord = 0;
    }
}

bool KeyboardMapper::IsEnabled() const noexcept { return m_enabled; }

void KeyboardMapper::SortBySpecificity() {
    std::stable_sort(m_bindings.begin(), m_bindings.end(),
        [](const KeyBinding& a, const KeyBinding& b) {
            return Popcount(a.mask) > Popcount(b.mask);
        });
}

int KeyboardMapper::Popcount(uint32_t v) noexcept {
    return static_cast<int>(std::popcount(v));
}

// ---------------------------------------------------------------------------
// Update — convenience dispatcher called from OnControllerState()
// ---------------------------------------------------------------------------

void KeyboardMapper::Update(const ControllerState& state) {
    if (!m_enabled) return;
    Press(state);
    Release(state);
}

// ---------------------------------------------------------------------------
// Press
// ---------------------------------------------------------------------------

void KeyboardMapper::Press(const ControllerState& state) {
    if (!m_enabled) return;

    const uint32_t curr   = static_cast<uint32_t>(state.buttons);
    const uint32_t rising = static_cast<uint32_t>(state.buttonsDown);
    m_prevButtons         = curr;

    if (rising == 0) return;

    for (const auto& b : m_bindings) {
        if ((curr   & b.mask) != b.mask) continue;
        if ((rising & b.mask) == 0)      continue;

        m_activeChord = b.mask;
        SendKeys(b.sequence);
        return;
    }
}

void KeyboardMapper::Release(const ControllerState& state) {
    if (!m_enabled || m_activeChord == 0) return;

    const uint32_t falling = static_cast<uint32_t>(state.buttonsUp);
    if ((falling & m_activeChord) == 0) return;

    for (const auto& b : m_bindings) {
        if (b.mask == m_activeChord) {
            SendKeysUp(b.sequence);
            break;
        }
    }
    m_activeChord = 0;
}

// ---------------------------------------------------------------------------
// SendKeys / SendKeysUp
// ---------------------------------------------------------------------------

void KeyboardMapper::SendKeys(const std::vector<VKey>& seq) {
    if (seq.empty()) return;

    std::vector<INPUT> inputs;
    inputs.reserve(seq.size());

    for (const auto& vk : seq) {
        INPUT inp{};
        inp.type       = INPUT_KEYBOARD;
        inp.ki.wVk     = vk.vk;
        inp.ki.dwFlags = vk.extended ? KEYEVENTF_EXTENDEDKEY : 0;
        inputs.push_back(inp);
    }
    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

void KeyboardMapper::SendKeysUp(const std::vector<VKey>& seq) {
    if (seq.empty()) return;

    std::vector<INPUT> inputs;
    inputs.reserve(seq.size());

    for (auto it = seq.rbegin(); it != seq.rend(); ++it) {
        INPUT inp{};
        inp.type       = INPUT_KEYBOARD;
        inp.ki.wVk     = it->vk;
        inp.ki.dwFlags = KEYEVENTF_KEYUP | (it->extended ? KEYEVENTF_EXTENDEDKEY : 0);
        inputs.push_back(inp);
    }
    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

} // namespace enjoystick::input
