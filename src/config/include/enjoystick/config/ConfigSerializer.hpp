#pragma once

#include <enjoystick/config/Config.hpp>

#include <string>

namespace enjoystick::config {

///
/// ConfigSerializer — JSON serialization for Config.
///
/// Both directions are designed to be forward-compatible:
///  - ToJson: emits all known fields.
///  - FromJson: reads only fields that exist; missing fields get defaults.
///    This means a config written by an older version of EnjoyStick will be
///    augmented with new defaults when read by a newer version.
///
struct ConfigSerializer {
    /// Serialize to a pretty-printed JSON string.
    [[nodiscard]] static std::string ToJson(const Config& cfg);

    /// Deserialize from JSON string. Throws std::runtime_error on bad JSON.
    [[nodiscard]] static Config FromJson(const std::string& json);
};

} // namespace enjoystick::config
