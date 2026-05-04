#pragma once

#include <enjoystick/shared/Types.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace enjoystick::input {

/// One key/modifier in a chord output sequence.
struct VKey {
    uint16_t vk   = 0;   ///< Windows virtual key code
    bool  extended = false;
};

/// A complete binding: when all buttons in `mask` are held AND rise together,
/// fire the `sequence` of VKeys.
struct KeyBinding {
    uint32_t          mask;      ///< OR of Button enum values
    std::vector<VKey> sequence; ///< keys to inject (pressed in order, released in reverse)
    std::string       name;     ///< human-readable label (for UI / logging)
};

///
/// KeyboardMapper — translates gamepad Button chords to keyboard INPUT events.
///
/// Chord disambiguation: bindings are evaluated most-specific-first
/// (by popcount of mask). A 3-button chord consumes the event and
/// prevents any 2-button or 1-button subset from also firing.
///
class KeyboardMapper {
public:
    KeyboardMapper() = default;

    /// Replace the entire binding table. Automatically sorts by specificity.
    void SetBindings(std::vector<KeyBinding> bindings);

    /// Add a single binding (re-sorts the table).
    void AddBinding(KeyBinding binding);

    /// Clear all bindings.
    void ClearBindings();

    /// Enable or disable all output. When disabled, Update() is a no-op.
    void SetEnabled(bool enabled) noexcept;
    [[nodiscard]] bool IsEnabled() const noexcept;

    /// Convenience: dispatches to Press() and Release() based on edge detection.
    /// Call once per input event from OnControllerState().
    void Update(const ControllerState& state);

    /// Call when new state arrives. Detects rising edges, resolves chords,
    /// and fires SendInput for matched bindings.
    void Press  (const ControllerState& state);
    void Release(const ControllerState& state);

    [[nodiscard]] const std::vector<KeyBinding>& GetBindings() const noexcept;

private:
    void SortBySpecificity();
    static void SendKeys  (const std::vector<VKey>& seq);
    static void SendKeysUp(const std::vector<VKey>& seq);
    static int  Popcount  (uint32_t v) noexcept;

    std::vector<KeyBinding> m_bindings;
    uint32_t                m_prevButtons = 0;
    uint32_t                m_activeChord = 0;
    bool                    m_enabled     = true;
};

} // namespace enjoystick::input
