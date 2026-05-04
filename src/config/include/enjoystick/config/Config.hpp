#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace enjoystick::config {

// ---------------------------------------------------------------------------
// Sub-configuration structs
// ---------------------------------------------------------------------------

struct MouseCfg {
    float maxSpeed   = 25.0f;
    float exponent   = 2.0f;
    float linearZone = 0.15f;
    bool  wrapEdges  = false;
    float scrollSpeed= 6.0f;
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

// Virtual key entry in a mapping sequence.
struct VKeyEntry {
    uint16_t vk      = 0;
    bool     extended= false;
};

// One gamepad-to-keyboard mapping.
struct KeyMappingEntry {
    uint32_t              buttonMask = 0;
    std::vector<VKeyEntry> sequence;
    std::string           name;
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

    // Default key mappings shipped with the app.
    static Config Defaults();
};

} // namespace enjoystick::config
