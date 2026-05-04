#pragma once

#include <enjoystick/config/Config.hpp>
#include <enjoystick/cursor/VirtualMouse.hpp>
#include <enjoystick/core/DeadzoneFilter.hpp>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

// Forward-declare the Windows HANDLE type so we don't pull in Windows.h here.
extern "C" { typedef void* HANDLE; }

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
/// ----- Low-level API (stable, used by tests and advanced callers) ----------
///   auto store = ConfigStore::Open(path);
///   auto h     = store->OnChange(callback);
///   store->Watch();
///   const Config& c = store->GetConfig();
///   store->Save(newConfig);
///   store->Unwatch();
///
/// ----- High-level convenience API (used by Application) -------------------
///   auto store = ConfigStore::Create();
///   store->Load();
///   store->StartWatcher();
///   const Config& cfg = store->Get();
///   store->SetCursorConfig(vmConfig);
///   store->SetDeadzoneConfig(dzConfig);
///   auto h = store->OnChanged(cb);   // alias for OnChange
///   store->StopWatcher();
///
class ConfigStore {
public:
    // -------------------------------------------------------------------------
    // Low-level factory
    // -------------------------------------------------------------------------

    /// Open (or create) config at the given path.
    [[nodiscard]] static std::unique_ptr<ConfigStore>
        Open(const std::filesystem::path& configPath);

    // -------------------------------------------------------------------------
    // High-level convenience factory
    // -------------------------------------------------------------------------

    /// Create a ConfigStore backed by the default location:
    ///   %APPDATA%\Enjoystick\config.json
    /// If the file does not exist, default values are written.
    [[nodiscard]] static std::unique_ptr<ConfigStore> Create();

    // -------------------------------------------------------------------------

    ~ConfigStore();

    // -------------------------------------------------------------------------
    // Low-level API
    // -------------------------------------------------------------------------

    /// Non-blocking access to the current Config snapshot (thread-safe).
    [[nodiscard]] const Config& GetConfig() const noexcept;

    /// Persist config to disk and fire OnChange callbacks.
    void Save(Config config);

    /// Start the ReadDirectoryChangesW watcher thread.
    void Watch();

    /// Stop the watcher thread. Idempotent.
    void Unwatch();

    /// Register a hot-reload callback.
    [[nodiscard]] ConfigCallbackHandle OnChange(ConfigChangedCallback cb);

    // -------------------------------------------------------------------------
    // High-level convenience API (used by Application.cpp)
    // -------------------------------------------------------------------------

    /// Alias for Open(defaultPath) — must be called after Create() returns.
    /// Provided only for symmetry; Create() already loads the file.
    void Load() { /* already loaded in Open() / Create() */ }

    /// Alias for Watch().
    void StartWatcher() { Watch(); }

    /// Alias for Unwatch().
    void StopWatcher() { Unwatch(); }

    /// Returns a const-ref to the current Config.  Alias for GetConfig().
    [[nodiscard]] const Config& Get() const noexcept { return GetConfig(); }

    /// Update only the cursor section and persist.
    void SetCursorConfig(const cursor::MouseConfig& mc);

    /// Update only the deadzone section and persist.
    void SetDeadzoneConfig(const core::DeadzoneConfig& dz);

    /// Alias for OnChange() — matches the name used in Application.cpp.
    template<typename Fn>
    [[nodiscard]] ConfigCallbackHandle OnChanged(Fn&& fn) {
        return OnChange(std::forward<Fn>(fn));
    }

private:
    explicit ConfigStore(std::filesystem::path path, Config initial);

    void WatchThread();
    bool ReloadFromDisk();

    std::filesystem::path  m_path;

    mutable std::atomic<std::shared_ptr<Config>> m_current;
    Config                    m_currentCopy;
    mutable std::shared_mutex m_configMutex;

    struct CB { uint64_t id; ConfigChangedCallback fn; };
    std::vector<CB>       m_callbacks;
    std::shared_mutex     m_cbMutex;
    uint64_t              m_nextCbId = 0;

    std::thread        m_watchThread;
    std::atomic<bool>  m_watching{false};
    HANDLE             m_dirHandle;
};

} // namespace enjoystick::config
