#pragma once

#include <enjoystick/input/KeyboardMapper.hpp>
#include <enjoystick/cursor/VirtualMouse.hpp>
#include <enjoystick/core/DeadzoneFilter.hpp>
#include <functional>
#include <filesystem>
#include <memory>
#include <string>

namespace enjoystick::config {

///
/// ConfigStore — owns and persists user settings.
///
/// File format: JSON, stored in %APPDATA%\Enjoystick\config.json
/// Hot-reload:  watches the config directory with ReadDirectoryChangesW;
///              registered listeners are notified on the watcher thread.
///
/// Design:
///   All sub-configurations are plain structs that survive serialisation
///   round-trips. The store validates values on load and clamps to sane
///   ranges rather than rejecting invalid configs, ensuring robustness.
///
struct AppConfig {
    cursor::VirtualMouse::Config  cursor;
    core::DeadzoneConfig          deadzone;
    // KeyBinding overrides are stored as a map: button_name -> vk_code
    // (KeyboardMapper applies them at runtime)
};

using ConfigChangedCallback = std::function<void(const AppConfig&)>;

class ConfigStore {
public:
    static std::unique_ptr<ConfigStore> Create();

    virtual ~ConfigStore() = default;

    /// Load config from disk. Creates default config if file does not exist.
    virtual void Load() = 0;

    /// Persist current config to disk immediately.
    virtual void Save() const = 0;

    /// Access the live config (read-only; modify via Set*).
    [[nodiscard]] virtual const AppConfig& Get() const noexcept = 0;

    /// Update cursor config section and persist.
    virtual void SetCursorConfig(cursor::VirtualMouse::Config cfg) = 0;

    /// Update deadzone config section and persist.
    virtual void SetDeadzoneConfig(core::DeadzoneConfig cfg) = 0;

    /// Register a callback fired whenever config changes (load or hot-reload).
    /// Returns a handle; destroying the handle unregisters the callback.
    [[nodiscard]] virtual CallbackHandle OnChanged(ConfigChangedCallback cb) = 0;

    /// Start file-system watcher for hot-reload.
    virtual void StartWatcher() = 0;
    virtual void StopWatcher()  = 0;

    /// Returns the config file path.
    [[nodiscard]] virtual std::filesystem::path GetConfigPath() const = 0;
};

} // namespace enjoystick::config
