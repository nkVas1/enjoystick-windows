#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace enjoystick::config {

// ---------------------------------------------------------------------------
// MouseCfg
// ---------------------------------------------------------------------------
struct MouseCfg {
    /// Base max speed in px/ms.  Adaptive scaling multiplies this per monitor.
    float maxSpeed          = 6.0f;
    /// Curve exponent: 1.35 gives gentle low-deflection roll-off.
    float exponent          = 1.35f;
    float linearZone        = 0.10f;
    bool  wrapEdges         = false;
    float scrollSpeed       = 4.0f;
    bool  triggersAsClicks  = false;
    bool  useRightStick     = true;
    float accelerationMs    = 80.0f;

    // Adaptive per-monitor calibration
    bool  adaptiveSpeed      = true;
    /// Time (ms) to cross the full screen width at max stick deflection.
    float targetTraversalMs  = 900.0f;
    /// DPI blend weight: 0.0 = pure resolution, 1.0 = pure DPI. Default 0.5.
    float dpiWeight          = 0.5f;
    float adaptiveMinScale   = 0.30f;
    float adaptiveMaxScale   = 2.50f;
};

// ---------------------------------------------------------------------------
// InputCfg
// ---------------------------------------------------------------------------
struct InputCfg {
    float pollingRateHz  = 250.0f;
    float deadzoneInner  = 0.08f;
    float deadzoneOuter  = 0.98f;
    bool  hapticsEnabled = true;
};

// ---------------------------------------------------------------------------
// OverlayCfg
// ---------------------------------------------------------------------------
struct OverlayCfg {
    bool        showActiveIndicator = true;
    uint32_t    monitorIndex        = 0;
    uint32_t    renderHz            = 60;
    std::string modeLabel;
};

// ---------------------------------------------------------------------------
// AppCfg
// ---------------------------------------------------------------------------
struct AppCfg {
    bool        autoStart      = false;
    bool        trayIcon       = true;
    bool        minimizeToTray = true;
    std::string language       = "en";
};

struct VKeyEntry {
    uint16_t vk       = 0;
    bool     extended = false;
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
