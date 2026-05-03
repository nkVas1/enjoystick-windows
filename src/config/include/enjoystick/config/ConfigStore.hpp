#pragma once

#include <enjoystick/cursor/VirtualMouse.hpp>
#include <enjoystick/core/InputEngine.hpp>
#include <enjoystick/core/DeadzoneFilter.hpp>
#include <functional>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <atomic>

namespace enjoystick::config {

///
/// AppConfig — the single aggregated configuration object.
/// All fields have sensible defaults so a missing config.json just works.
///
struct AppConfig {
    core::InputEngine::Config    input     = {};
    core::DeadzoneConfig         deadzone  = {};
    cursor::VirtualMouse::Config cursor    = {};

    struct Hotkeys {
        bool guideOpensRadialMenu = true;   ///< Guide / PS button opens OSK
        bool startOpensTaskbar   = true;    ///< Start opens Windows taskbar
        bool selectOpensModes    = true;    ///< Select / Back cycles overlay modes
    } hotkeys;

    struct UI {
        bool showHUDOnConnect  = true;
        float hudAutohideMs    = 4000.0f;
        bool toastsEnabled     = true;
    } ui;

    struct Autostart {
        bool enabled           = true;
        bool minimizeToTray    = true;
    } autostart;
};

///
/// ConfigStore — loads/saves config.json and watches the file for changes.
///
/// Thread safety: GetConfig() and SetConfig() are fully thread-safe.
/// Change callbacks are dispatched on a background watcher thread.
///
class ConfigStore {
public:
    using ChangeCallback = std::function<void(const AppConfig&)>;

    /// configDir: directory containing config.json (created if absent).
    explicit ConfigStore(std::filesystem::path configDir);
    ~ConfigStore();

    ConfigStore(const ConfigStore&)            = delete;
    ConfigStore& operator=(const ConfigStore&) = delete;

    /// Load from disk. Returns false if file is missing (defaults are kept).
    bool Load();

    /// Persist current config to disk.
    bool Save() const;

    [[nodiscard]] AppConfig GetConfig() const;
    void SetConfig(AppConfig config);

    /// Register a callback invoked whenever the config changes (load or SetConfig).
    void OnChange(ChangeCallback cb);

    /// Start the file-system watcher (ReadDirectoryChangesW).
    void StartWatcher();
    void StopWatcher();

private:
    void WatchLoop();
    void NotifyChange();

    [[nodiscard]] std::filesystem::path ConfigFilePath() const;

    std::filesystem::path   m_configDir;
    mutable std::mutex      m_mutex;
    AppConfig               m_config;

    std::vector<ChangeCallback> m_callbacks;
    mutable std::mutex          m_callbackMutex;

    std::atomic<bool> m_watching{false};
    std::thread       m_watchThread;
    HANDLE            m_watchDir = INVALID_HANDLE_VALUE;
};

} // namespace enjoystick::config
