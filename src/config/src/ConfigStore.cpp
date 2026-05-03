#include <enjoystick/config/ConfigStore.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <fstream>
#include <sstream>
#include <stdexcept>

// Minimal JSON read/write without external dependencies.
// We use a hand-rolled approach to avoid pulling in nlohmann or similar.
// For a production build we would vendor nlohmann/json (MIT) via vcpkg;
// this stub provides the API contract and safe defaults.

namespace enjoystick::config {

static const wchar_t kConfigFile[] = L"config.json";

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ConfigStore::ConfigStore(std::filesystem::path configDir)
    : m_configDir(std::move(configDir)) {
    std::filesystem::create_directories(m_configDir);
}

ConfigStore::~ConfigStore() {
    StopWatcher();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::filesystem::path ConfigStore::ConfigFilePath() const {
    return m_configDir / "config.json";
}

// ---------------------------------------------------------------------------
// Load / Save  (human-readable JSON, hand-written serializer)
// ---------------------------------------------------------------------------

bool ConfigStore::Load() {
    const auto path = ConfigFilePath();
    if (!std::filesystem::exists(path)) {
        Save(); // write defaults
        return false;
    }

    std::ifstream f(path);
    if (!f.is_open()) return false;

    // Minimal key-value extraction for our flat config.
    // A full implementation would use nlohmann::json from vcpkg.
    std::string line;
    std::lock_guard lock(m_mutex);
    while (std::getline(f, line)) {
        auto readFloat = [&](const char* key, float& out) {
            const auto pos = line.find(key);
            if (pos == std::string::npos) return;
            const auto colon = line.find(':', pos);
            if (colon == std::string::npos) return;
            try { out = std::stof(line.substr(colon + 1)); } catch (...) {}
        };
        auto readBool = [&](const char* key, bool& out) {
            const auto pos = line.find(key);
            if (pos == std::string::npos) return;
            out = line.find("true", pos) != std::string::npos;
        };
        auto readUint = [&](const char* key, uint32_t& out) {
            const auto pos = line.find(key);
            if (pos == std::string::npos) return;
            const auto colon = line.find(':', pos);
            if (colon == std::string::npos) return;
            try { out = static_cast<uint32_t>(std::stoul(line.substr(colon + 1))); } catch (...) {}
        };

        readUint("pollingRateHz",       m_config.input.pollingRateHz);
        readBool("hapticsEnabled",      m_config.input.hapticsEnabled);
        readFloat("innerRadius",        m_config.deadzone.innerRadius);
        readFloat("outerRadius",        m_config.deadzone.outerRadius);
        readFloat("maxSpeedPx",         m_config.cursor.maxSpeedPx);
        readFloat("curveExponent",      m_config.cursor.curveExponent);
        readFloat("accelerationMs",     m_config.cursor.accelerationMs);
        readBool("useRightStick",       m_config.cursor.useRightStick);
        readBool("triggersAsClicks",    m_config.cursor.triggersAsClicks);
        readFloat("scrollSpeed",        m_config.cursor.scrollSpeed);
        readBool("invertScroll",        m_config.cursor.invertScroll);
        readBool("showHUDOnConnect",    m_config.ui.showHUDOnConnect);
        readFloat("hudAutohideMs",      m_config.ui.hudAutohideMs);
        readBool("toastsEnabled",       m_config.ui.toastsEnabled);
        readBool("autostartEnabled",    m_config.autostart.enabled);
        readBool("minimizeToTray",      m_config.autostart.minimizeToTray);
    }

    NotifyChange();
    return true;
}

bool ConfigStore::Save() const {
    const auto path = ConfigFilePath();
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) return false;

    std::lock_guard lock(m_mutex);
    const auto& c = m_config;
    f << "{\n";
    f << "  \"pollingRateHz\": "    << c.input.pollingRateHz         << ",\n";
    f << "  \"hapticsEnabled\": "   << (c.input.hapticsEnabled ? "true" : "false") << ",\n";
    f << "  \"innerRadius\": "      << c.deadzone.innerRadius          << ",\n";
    f << "  \"outerRadius\": "      << c.deadzone.outerRadius          << ",\n";
    f << "  \"maxSpeedPx\": "       << c.cursor.maxSpeedPx             << ",\n";
    f << "  \"curveExponent\": "    << c.cursor.curveExponent           << ",\n";
    f << "  \"accelerationMs\": "   << c.cursor.accelerationMs          << ",\n";
    f << "  \"useRightStick\": "    << (c.cursor.useRightStick ? "true" : "false") << ",\n";
    f << "  \"triggersAsClicks\": " << (c.cursor.triggersAsClicks ? "true" : "false") << ",\n";
    f << "  \"scrollSpeed\": "      << c.cursor.scrollSpeed             << ",\n";
    f << "  \"invertScroll\": "     << (c.cursor.invertScroll ? "true" : "false") << ",\n";
    f << "  \"showHUDOnConnect\": " << (c.ui.showHUDOnConnect ? "true" : "false") << ",\n";
    f << "  \"hudAutohideMs\": "    << c.ui.hudAutohideMs              << ",\n";
    f << "  \"toastsEnabled\": "    << (c.ui.toastsEnabled ? "true" : "false") << ",\n";
    f << "  \"autostartEnabled\": " << (c.autostart.enabled ? "true" : "false") << ",\n";
    f << "  \"minimizeToTray\": "   << (c.autostart.minimizeToTray ? "true" : "false") << "\n";
    f << "}\n";
    return true;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

AppConfig ConfigStore::GetConfig() const {
    std::lock_guard lock(m_mutex);
    return m_config;
}

void ConfigStore::SetConfig(AppConfig config) {
    {
        std::lock_guard lock(m_mutex);
        m_config = std::move(config);
    }
    Save();
    NotifyChange();
}

void ConfigStore::OnChange(ChangeCallback cb) {
    std::lock_guard lock(m_callbackMutex);
    m_callbacks.push_back(std::move(cb));
}

void ConfigStore::NotifyChange() {
    const AppConfig snap = GetConfig();
    std::lock_guard lock(m_callbackMutex);
    for (auto& cb : m_callbacks) { if (cb) cb(snap); }
}

// ---------------------------------------------------------------------------
// File-system watcher (ReadDirectoryChangesW)
// ---------------------------------------------------------------------------

void ConfigStore::StartWatcher() {
    if (m_watching.exchange(true)) return;

    m_watchDir = CreateFileW(
        m_configDir.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr);

    if (m_watchDir == INVALID_HANDLE_VALUE) {
        m_watching = false;
        return;
    }

    m_watchThread = std::thread([this] { WatchLoop(); });
}

void ConfigStore::StopWatcher() {
    if (!m_watching.exchange(false)) return;
    if (m_watchDir != INVALID_HANDLE_VALUE) {
        CancelIoEx(m_watchDir, nullptr);
        CloseHandle(m_watchDir);
        m_watchDir = INVALID_HANDLE_VALUE;
    }
    if (m_watchThread.joinable()) m_watchThread.join();
}

void ConfigStore::WatchLoop() {
    alignas(DWORD) char buf[4096];
    DWORD bytesReturned = 0;
    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    while (m_watching.load(std::memory_order_relaxed)) {
        ResetEvent(ov.hEvent);
        ReadDirectoryChangesW(
            m_watchDir, buf, sizeof(buf),
            FALSE,
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME,
            nullptr, &ov, nullptr);

        const DWORD wait = WaitForSingleObject(ov.hEvent, 500);
        if (wait == WAIT_OBJECT_0) {
            GetOverlappedResult(m_watchDir, &ov, &bytesReturned, FALSE);
            // Reload on any change in the directory
            Load();
        }
    }
    CloseHandle(ov.hEvent);
}

} // namespace enjoystick::config
