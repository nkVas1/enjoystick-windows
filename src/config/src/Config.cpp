// Config.cpp — default configuration values.
//
// VK_LWIN and other virtual-key constants require <Windows.h>.
// WIN32_LEAN_AND_MEAN / NOMINMAX are injected globally by CMake so we do not
// redeclare them here.
#include <Windows.h>

#include "enjoystick/config/Config.hpp"
#include "enjoystick/shared/Types.hpp"   // Button enum (for button-mask values)

namespace enjoystick::config {

// Helper: cast Button bitmask to the uint32_t stored in KeyMappingEntry.
static constexpr uint32_t BM(enjoystick::Button b) noexcept {
    return static_cast<uint32_t>(b);
}

Config Config::Defaults() {
    using B = enjoystick::Button;

    Config c;

    // -----------------------------------------------------------------
    // Navigate-mode default key mappings
    // -----------------------------------------------------------------

    // D-pad → arrow keys
    c.keyMappings.push_back({
        BM(B::DPadUp),
        {{ VK_UP, true }},
        "D-pad Up -> Arrow Up"
    });
    c.keyMappings.push_back({
        BM(B::DPadDown),
        {{ VK_DOWN, true }},
        "D-pad Down -> Arrow Down"
    });
    c.keyMappings.push_back({
        BM(B::DPadLeft),
        {{ VK_LEFT, true }},
        "D-pad Left -> Arrow Left"
    });
    c.keyMappings.push_back({
        BM(B::DPadRight),
        {{ VK_RIGHT, true }},
        "D-pad Right -> Arrow Right"
    });

    // Face buttons
    c.keyMappings.push_back({
        BM(B::South),                               // A / Cross
        {{ VK_RETURN, true }},
        "A -> Enter"
    });
    c.keyMappings.push_back({
        BM(B::East),                                // B / Circle
        {{ VK_ESCAPE, false }},
        "B -> Escape"
    });
    c.keyMappings.push_back({
        BM(B::West),                                // X / Square -> Space
        {{ VK_SPACE, false }},
        "X -> Space"
    });
    c.keyMappings.push_back({
        BM(B::North),                               // Y / Triangle -> F5 (refresh)
        {{ VK_F5, true }},
        "Y -> F5"
    });

    // Shoulder buttons — Tab / Shift+Tab
    c.keyMappings.push_back({
        BM(B::RB),
        {{ VK_TAB, true }},
        "RB -> Tab"
    });
    c.keyMappings.push_back({
        BM(B::LB),
        {{ static_cast<uint16_t>(VK_SHIFT), false },
         { VK_TAB, true }},
        "LB -> Shift+Tab"
    });

    // Start → Win key (open Start Menu)
    c.keyMappings.push_back({
        BM(B::Start),
        {{ static_cast<uint16_t>(VK_LWIN), false }},
        "Start -> Windows key"
    });

    // Start + Select → Win+G (Xbox Game Bar) — chord, higher specificity
    c.keyMappings.push_back({
        BM(B::Start) | BM(B::Select),
        {{ static_cast<uint16_t>(VK_LWIN), false },
         { static_cast<uint16_t>('G'), false }},
        "Start+Select -> Xbox Game Bar"
    });

    return c;
}

} // namespace enjoystick::config
