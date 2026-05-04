#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace enjoystick::config {

// ---------------------------------------------------------------------------
// Sub-configuration structs
// ---------------------------------------------------------------------------

struct MouseCfg {
    /// px/ms at full deflection. 12 is comfortable for 1080p desktop use.
    float maxSpeed         = 12.0f;
    /// Curve exponent: 1.6 gives a gentle roll-off without being too linear.
    float exponent         = 1.6f;
    float linearZone       = 0.12f;
    bool  wrapEdges        = false;
    float scrollSpeed      = 4.0f;

    // Fields kept in sync with cursor::MouseConfig
    bool  triggersAsClicks = false;  ///< map LT/RT analogue to mouse buttons
    bool  useRightStick    = true;   ///< false → use left stick for cursor
    /// Ramp duration in ms. 60 ms prevents the jarring lurch on first push.
    float accelerationMs   = 60.0f;
};

struct InputCfg {
    float pollingRateHz = 250.0f;
    float deadzoneInner = 0.08f;
    float deadzoneOuter = 0.98f;
    bool  hapticsEnabled= true;
};

struct OverlayCfg {
    bool        showActiveIndicator = true;
    uint32_t    monitorIndex        = 0;
    uint32_t    renderHz            = 60;
    std::string modeLabel;
};

struct AppCfg {
    bool        autoStart      = false;
    bool        trayIcon       = true;
    bool        minimizeToTray = true;
    std::string language       = "en";
};

struct VKeyEntry {
    uint16_t vk      = 0;
    bool     extended= false;
};

struct KeyMappingEntry {
    uint32_t               buttonMask = 0;
    std::vector<VKeyEntry> sequence;
    std::string            name;
};

// ---------------------------------------------------------------------------
// Root config
// ---------------------------------------------------------------------------

struct Config {
    MouseCfg  mouse;
    InputCfg  input;
    OverlayCfg overlay;
    AppCfg    app;
    std::vector<KeyMappingEntry> keyMappings;

    static Config Defaults();
};

} // namespace enjoystick::config
