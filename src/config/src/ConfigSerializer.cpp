#include "enjoystick/config/ConfigSerializer.hpp"
#include "enjoystick/config/Config.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

namespace enjoystick::config {

template<typename T>
static void Read(const json& j, const char* key, T& out) {
    if (j.contains(key)) j.at(key).get_to(out);
}

// ---------------------------------------------------------------------------
// ToJson
// ---------------------------------------------------------------------------

std::string ConfigSerializer::ToJson(const Config& cfg) {
    json j;

    j["mouse"]["max_speed"]           = cfg.mouse.maxSpeed;
    j["mouse"]["exponent"]            = cfg.mouse.exponent;
    j["mouse"]["linear_zone"]         = cfg.mouse.linearZone;
    j["mouse"]["wrap_edges"]          = cfg.mouse.wrapEdges;
    j["mouse"]["scroll_speed"]        = cfg.mouse.scrollSpeed;
    j["mouse"]["triggers_as_clicks"]  = cfg.mouse.triggersAsClicks;
    j["mouse"]["use_right_stick"]     = cfg.mouse.useRightStick;
    j["mouse"]["acceleration_ms"]     = cfg.mouse.accelerationMs;

    j["input"]["polling_rate_hz"] = cfg.input.pollingRateHz;
    j["input"]["deadzone_inner"]  = cfg.input.deadzoneInner;
    j["input"]["deadzone_outer"]  = cfg.input.deadzoneOuter;
    j["input"]["haptics_enabled"] = cfg.input.hapticsEnabled;

    j["overlay"]["show_active_indicator"] = cfg.overlay.showActiveIndicator;
    j["overlay"]["monitor_index"]         = cfg.overlay.monitorIndex;
    j["overlay"]["render_hz"]             = cfg.overlay.renderHz;
    j["overlay"]["mode_label"]            = cfg.overlay.modeLabel;

    j["app"]["auto_start"]       = cfg.app.autoStart;
    j["app"]["tray_icon"]        = cfg.app.trayIcon;
    j["app"]["language"]         = cfg.app.language;
    j["app"]["minimize_to_tray"] = cfg.app.minimizeToTray;

    json mappings = json::array();
    for (const auto& km : cfg.keyMappings) {
        json m;
        m["name"]    = km.name;
        m["buttons"] = km.buttonMask;
        json seq = json::array();
        for (const auto& vk : km.sequence) {
            json v;
            v["vk"]       = vk.vk;
            v["extended"] = vk.extended;
            seq.push_back(v);
        }
        m["sequence"] = seq;
        mappings.push_back(m);
    }
    j["key_mappings"] = mappings;

    return j.dump(4);
}

// ---------------------------------------------------------------------------
// FromJson
// ---------------------------------------------------------------------------

Config ConfigSerializer::FromJson(const std::string& jsonStr) {
    json j;
    try {
        j = json::parse(jsonStr);
    } catch (const json::exception& e) {
        throw std::runtime_error(
            std::string("ConfigSerializer: JSON parse error: ") + e.what());
    }

    Config cfg;

    if (j.contains("mouse")) {
        const auto& m = j["mouse"];
        Read(m, "max_speed",          cfg.mouse.maxSpeed);
        Read(m, "exponent",           cfg.mouse.exponent);
        Read(m, "linear_zone",        cfg.mouse.linearZone);
        Read(m, "wrap_edges",         cfg.mouse.wrapEdges);
        Read(m, "scroll_speed",       cfg.mouse.scrollSpeed);
        Read(m, "triggers_as_clicks", cfg.mouse.triggersAsClicks);
        Read(m, "use_right_stick",    cfg.mouse.useRightStick);
        Read(m, "acceleration_ms",    cfg.mouse.accelerationMs);
    }

    if (j.contains("input")) {
        const auto& inp = j["input"];
        Read(inp, "polling_rate_hz", cfg.input.pollingRateHz);
        Read(inp, "deadzone_inner",  cfg.input.deadzoneInner);
        Read(inp, "deadzone_outer",  cfg.input.deadzoneOuter);
        Read(inp, "haptics_enabled", cfg.input.hapticsEnabled);
    }

    if (j.contains("overlay")) {
        const auto& ov = j["overlay"];
        Read(ov, "show_active_indicator", cfg.overlay.showActiveIndicator);
        Read(ov, "monitor_index",         cfg.overlay.monitorIndex);
        Read(ov, "render_hz",             cfg.overlay.renderHz);
        Read(ov, "mode_label",            cfg.overlay.modeLabel);
    }

    if (j.contains("app")) {
        const auto& a = j["app"];
        Read(a, "auto_start",       cfg.app.autoStart);
        Read(a, "tray_icon",        cfg.app.trayIcon);
        Read(a, "language",         cfg.app.language);
        Read(a, "minimize_to_tray", cfg.app.minimizeToTray);
    }

    if (j.contains("key_mappings") && j["key_mappings"].is_array()) {
        cfg.keyMappings.clear();
        for (const auto& item : j["key_mappings"]) {
            KeyMappingEntry km;
            Read(item, "name",    km.name);
            Read(item, "buttons", km.buttonMask);
            if (item.contains("sequence") && item["sequence"].is_array()) {
                for (const auto& vkj : item["sequence"]) {
                    VKeyEntry vk;
                    Read(vkj, "vk",       vk.vk);
                    Read(vkj, "extended", vk.extended);
                    km.sequence.push_back(vk);
                }
            }
            cfg.keyMappings.push_back(std::move(km));
        }
    }

    return cfg;
}

} // namespace enjoystick::config
