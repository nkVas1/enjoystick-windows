#include "enjoystick/config/Config.hpp"

namespace enjoystick::config {

Config Config::Defaults() {
    Config c;
    // Mouse mode: LT = left click, RT = right click, RS = scroll
    // Keyboard: Start+Select = Win+G (Xbox Game Bar), Guide = Win
    c.keyMappings = {
        // Start + Select → Win+G (Xbox Game Bar)
        {
            (1u << 5) | (1u << 4),  // Button::Start | Button::Select
            {{ VK_LWIN, false }, { 'G', false }},
            "Xbox Game Bar"
        },
        // Guide → Win key
        {
            (1u << 12),             // Button::Guide
            {{ VK_LWIN, false }},
            "Windows key"
        },
    };
    return c;
}

} // namespace enjoystick::config
