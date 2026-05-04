#pragma once

#include <enjoystick/config/Config.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>
#include <filesystem>

namespace enjoystick::config {

/// Signature: void(const Config&)
using ConfigChangedCallback = std::function<void(const Config&)>;

///
/// RAII handle returned by OnChange(). Destructor unregisters the callback.
///
class ConfigCallbackHandle {
public:
    ConfigCallbackHandle() = default;
    explicit ConfigCallbackHandle(std::function<void()> unreg)
        : m_unreg(std::move(unreg)) {}
    ~ConfigCallbackHandle() { if (m_unreg) m_unreg(); }

    ConfigCallbackHandle(const ConfigCallbackHandle&)            = delete;
    ConfigCallbackHandle& operator=(const ConfigCallbackHandle&) = delete;
    ConfigCallbackHandle(ConfigCallbackHandle&&)                 = default;
    ConfigCallbackHandle& operator=(ConfigCallbackHandle&&)      = default;

private:
    std::function<void()> m_unreg;
};

///
/// ConfigStore — loads, saves, and hot-reloads config.json.
///
/// Usage:
///   auto store = ConfigStore::Open(path);    // load or create default
///   auto h     = store->OnChange(callback);   // hot-reload subscription
///   store->Watch();                           // start FS watcher
///   const Config& c = store->GetConfig();     // current snapshot
///   store->Save(newConfig);                   // atomic save + reload
///   store->Unwatch();                         // stop FS watcher
///
class ConfigStore {
public:
    /// Open (or create) config at the given path.
    [[nodiscard]] static std::unique_ptr<ConfigStore>
        Open(const std::filesystem::path& configPath);

    ~ConfigStore();

    /// Non-blocking access to the current Config snapshot.
    /// Thread-safe; lock-free on read path.
    [[nodiscard]] const Config& GetConfig() const noexcept;

    /// Persist config to disk and fire OnChange callbacks.
    void Save(Config config);

    /// Start the ReadDirectoryChangesW watcher thread.
    void Watch();

    /// Stop the watcher thread. Idempotent.
    void Unwatch();

    /// Register a hot-reload callback.
    /// The callback is fired on the watcher thread with a shared read-lock.
    /// @returns a RAII handle; let it go out of scope to unregister.
    [[nodiscard]] ConfigCallbackHandle OnChange(ConfigChangedCallback cb);

private:
    explicit ConfigStore(std::filesystem::path path, Config initial);

    void WatchThread();
    bool ReloadFromDisk();

    std::filesystem::path  m_path;   ///< full path to config.json

    // Current config — shared_ptr for cheap atomic swap.
    // C++20: std::atomic<std::shared_ptr<Config>> is spec-mandated lock-free
    // on MSVC with /std:c++20 when the pointer fits in a machine word.
    mutable std::atomic<std::shared_ptr<Config>> m_current;
    // Non-atomic copy for GetConfig() return-by-ref (updated under write-lock)
    Config                   m_currentCopy;
    mutable std::shared_mutex m_configMutex;

    // Callback registry
    struct CB { uint64_t id; ConfigChangedCallback fn; };
    std::vector<CB>       m_callbacks;
    std::shared_mutex     m_cbMutex;
    uint64_t              m_nextCbId = 0;

    // Watcher thread
    std::thread        m_watchThread;
    std::atomic<bool>  m_watching{false};
    HANDLE             m_dirHandle = INVALID_HANDLE_VALUE;  ///< for CancelIoEx
};

} // namespace enjoystick::config
