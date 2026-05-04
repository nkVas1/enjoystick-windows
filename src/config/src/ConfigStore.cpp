// WIN32_LEAN_AND_MEAN and NOMINMAX injected by CMake.
#include <Windows.h>

#include "enjoystick/config/ConfigStore.hpp"
#include "enjoystick/config/ConfigSerializer.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace enjoystick::config {

// ---------------------------------------------------------------------------
// Default path helper
// ---------------------------------------------------------------------------

static std::filesystem::path DefaultConfigPath() {
    wchar_t* appData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) {
        std::filesystem::path p(appData);
        CoTaskMemFree(appData);
        return p / L"Enjoystick" / L"config.json";
    }
    // Fallback: next to the executable
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    return std::filesystem::path(exePath).parent_path() / L"config.json";
}

// ---------------------------------------------------------------------------
// Open (factory)
// ---------------------------------------------------------------------------

std::unique_ptr<ConfigStore>
ConfigStore::Open(const std::filesystem::path& configPath) {
    Config initial;
    bool loaded = false;

    std::error_code ec;
    if (std::filesystem::exists(configPath, ec)) {
        std::ifstream f(configPath);
        if (f.is_open()) {
            std::ostringstream ss;
            ss << f.rdbuf();
            try {
                initial = ConfigSerializer::FromJson(ss.str());
                loaded  = true;
            } catch (...) { /* fallback to defaults */ }
        }
    }

    if (!loaded) {
        initial = Config{};
        std::filesystem::create_directories(configPath.parent_path(), ec);
        std::ofstream out(configPath);
        if (out.is_open()) out << ConfigSerializer::ToJson(initial);
    }

    return std::unique_ptr<ConfigStore>(new ConfigStore(configPath, std::move(initial)));
}

// ---------------------------------------------------------------------------
// Create (convenience factory — uses %APPDATA%\Enjoystick\config.json)
// ---------------------------------------------------------------------------

std::unique_ptr<ConfigStore> ConfigStore::Create() {
    return Open(DefaultConfigPath());
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

ConfigStore::ConfigStore(std::filesystem::path path, Config initial)
    : m_path(std::move(path))
    , m_currentCopy(initial)
    , m_dirHandle(INVALID_HANDLE_VALUE)
{
    m_current.store(std::make_shared<Config>(std::move(initial)),
                    std::memory_order_release);
}

ConfigStore::~ConfigStore() {
    Unwatch();
}

// ---------------------------------------------------------------------------
// GetConfig (read-path)
// ---------------------------------------------------------------------------

const Config& ConfigStore::GetConfig() const noexcept {
    std::shared_lock lock(m_configMutex);
    return m_currentCopy;
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

void ConfigStore::Save(Config config) {
    const auto tmp = std::filesystem::path(m_path).replace_extension(".json.tmp");
    {
        std::ofstream out(tmp);
        if (!out.is_open()) return;
        out << ConfigSerializer::ToJson(config);
    }
    std::error_code ec;
    std::filesystem::rename(tmp, m_path, ec);

    {
        std::unique_lock lock(m_configMutex);
        m_currentCopy = config;
        m_current.store(std::make_shared<Config>(config), std::memory_order_release);
    }

    {
        std::shared_lock lock(m_cbMutex);
        for (const auto& cb : m_callbacks) {
            if (cb.fn) cb.fn(config);
        }
    }
}

// ---------------------------------------------------------------------------
// Convenience setters
// ---------------------------------------------------------------------------

void ConfigStore::SetCursorConfig(const cursor::MouseConfig& mc) {
    Config cfg;
    {
        std::shared_lock lock(m_configMutex);
        cfg = m_currentCopy;
    }
    cfg.mouse.maxSpeed    = mc.maxSpeedPx;
    cfg.mouse.exponent    = mc.curveExponent;
    cfg.mouse.linearZone  = mc.linearZone;
    cfg.mouse.scrollSpeed = mc.scrollSpeed;
    cfg.mouse.wrapEdges   = mc.wrapEdges;
    Save(std::move(cfg));
}

void ConfigStore::SetDeadzoneConfig(const core::DeadzoneConfig& dz) {
    Config cfg;
    {
        std::shared_lock lock(m_configMutex);
        cfg = m_currentCopy;
    }
    cfg.input.deadzoneInner = dz.innerRadius;
    cfg.input.deadzoneOuter = dz.outerRadius;
    Save(std::move(cfg));
}

// ---------------------------------------------------------------------------
// OnChange
// ---------------------------------------------------------------------------

ConfigCallbackHandle ConfigStore::OnChange(ConfigChangedCallback cb) {
    std::unique_lock lock(m_cbMutex);
    const uint64_t id = ++m_nextCbId;
    m_callbacks.push_back({id, std::move(cb)});
    return ConfigCallbackHandle([this, id] {
        std::unique_lock lk(m_cbMutex);
        for (auto& e : m_callbacks) {
            if (e.id == id) { e.fn = nullptr; return; }
        }
    });
}

// ---------------------------------------------------------------------------
// Watch / Unwatch
// ---------------------------------------------------------------------------

void ConfigStore::Watch() {
    if (m_watching.exchange(true)) return;
    m_watchThread = std::thread([this] { WatchThread(); });
}

void ConfigStore::Unwatch() {
    if (!m_watching.exchange(false)) return;
    if (m_dirHandle != INVALID_HANDLE_VALUE)
        CancelIoEx(m_dirHandle, nullptr);
    if (m_watchThread.joinable()) m_watchThread.join();
}

// ---------------------------------------------------------------------------
// WatchThread
// ---------------------------------------------------------------------------

void ConfigStore::WatchThread() {
    const std::filesystem::path dir  = m_path.parent_path();
    const std::wstring          file = m_path.filename().wstring();

    m_dirHandle = CreateFileW(
        dir.wstring().c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);

    if (m_dirHandle == INVALID_HANDLE_VALUE) {
        m_watching = false;
        return;
    }

    static constexpr DWORD kBufSize = 8 * 1024;
    alignas(DWORD) uint8_t buf[kBufSize];

    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    while (m_watching.load(std::memory_order_relaxed)) {
        ResetEvent(ov.hEvent);
        const BOOL ok = ReadDirectoryChangesW(
            m_dirHandle, buf, kBufSize,
            FALSE,
            FILE_NOTIFY_CHANGE_LAST_WRITE,
            nullptr, &ov, nullptr);

        if (!ok) break;

        const DWORD wait = WaitForSingleObject(ov.hEvent, INFINITE);
        if (wait != WAIT_OBJECT_0) break;

        DWORD transferred = 0;
        if (!GetOverlappedResult(m_dirHandle, &ov, &transferred, FALSE)) break;
        if (transferred == 0) continue;

        const uint8_t* ptr = buf;
        for (;;) {
            const auto* fni =
                reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(ptr);

            if (fni->Action == FILE_ACTION_MODIFIED) {
                const std::wstring changed(
                    fni->FileName,
                    fni->FileNameLength / sizeof(wchar_t));
                if (_wcsicmp(changed.c_str(), file.c_str()) == 0) {
                    Sleep(80);
                    ReloadFromDisk();
                }
            }

            if (fni->NextEntryOffset == 0) break;
            ptr += fni->NextEntryOffset;
        }
    }

    CloseHandle(ov.hEvent);
    CloseHandle(m_dirHandle);
    m_dirHandle = INVALID_HANDLE_VALUE;
    m_watching  = false;
}

// ---------------------------------------------------------------------------
// ReloadFromDisk
// ---------------------------------------------------------------------------

bool ConfigStore::ReloadFromDisk() {
    std::ifstream f(m_path);
    if (!f.is_open()) return false;

    std::ostringstream ss;
    ss << f.rdbuf();
    f.close();

    Config parsed;
    try {
        parsed = ConfigSerializer::FromJson(ss.str());
    } catch (...) {
        return false;
    }

    {
        std::unique_lock lock(m_configMutex);
        m_currentCopy = parsed;
        m_current.store(std::make_shared<Config>(parsed), std::memory_order_release);
    }

    {
        std::shared_lock lock(m_cbMutex);
        for (const auto& cb : m_callbacks) {
            if (cb.fn) cb.fn(parsed);
        }
    }
    return true;
}

} // namespace enjoystick::config
