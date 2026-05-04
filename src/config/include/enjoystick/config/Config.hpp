#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace enjoystick::config {

struct MouseCfg {
    float maxSpeed         = 4.2f;
    float exponent         = 1.30f;
    float linearZone       = 0.09f;
    bool  wrapEdges        = false;
    float scrollSpeed      = 4.0f;

    bool  triggersAsClicks = false;
    bool  useRightStick    = true;
    float accelerationMs   = 95.0f;

    bool  adaptiveSpeed    = true;
    float targetScreenFracPerSec = 0.26f;
    float adaptiveMinScale = 0.38f;
    float adaptiveMaxScale = 0.92f;
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

struct Config {
    MouseCfg  mouse;
    InputCfg  input;
    OverlayCfg overlay;
    AppCfg    app;
    std::vector<KeyMappingEntry> keyMappings;

    static Config Defaults();
};

} // namespace enjoystick::config
