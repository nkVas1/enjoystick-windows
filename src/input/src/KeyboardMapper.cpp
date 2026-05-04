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

void KeyboardMapper::SortBySpecificity() {
    std::stable_sort(m_bindings.begin(), m_bindings.end(),
        [](const KeyBinding& a, const KeyBinding& b) {
            return Popcount(a.mask) > Popcount(b.mask);  // most bits first
        });
}

int KeyboardMapper::Popcount(uint32_t v) noexcept {
    return static_cast<int>(std::popcount(v));
}

// ---------------------------------------------------------------------------
// Press — called on each input event
//
// Strategy:
//   1. Compute rising edges (buttons that just went down).
//   2. current = all currently-held buttons.
//   3. Walk bindings most-specific-first.
//   4. If binding.mask is a subset of current AND all bits in mask rose this
//      frame — fire it. Record m_activeChord so Release knows what to lift.
//   5. Only fire the FIRST (most-specific) match.
// ---------------------------------------------------------------------------

void KeyboardMapper::Press(const core::ControllerState& state) {
    const uint32_t curr    = static_cast<uint32_t>(state.buttons);
    const uint32_t rising  = static_cast<uint32_t>(state.buttonsDown);
    m_prevButtons          = curr;

    if (rising == 0) return;  // nothing new pressed

    for (const auto& b : m_bindings) {
        // All mask bits must be currently held
        if ((curr & b.mask) != b.mask) continue;
        // At least one bit of mask must have just risen this frame
        if ((rising & b.mask) == 0) continue;

        // Match! Consume and send.
        m_activeChord = b.mask;
        SendKeys(b.sequence);
        return;  // most-specific-first: done
    }
}

void KeyboardMapper::Release(const core::ControllerState& state) {
    if (m_activeChord == 0) return;

    const uint32_t curr    = static_cast<uint32_t>(state.buttons);
    const uint32_t falling = static_cast<uint32_t>(state.buttonsUp);

    // Release when any bit of the active chord has fallen
    if ((falling & m_activeChord) == 0) return;

    // Find the binding and send key-up in reverse order
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
//
// We batch all KEYDOWNs in one SendInput call for atomicity (prevents the
// OS from inserting other events between them at the driver level).
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

    // Release in reverse order (e.g., Shift+Alt+T: up T, up Alt, up Shift)
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
