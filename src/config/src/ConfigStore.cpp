#include <enjoystick/config/ConfigStore.hpp>

// WIN32_LEAN_AND_MEAN is injected by CMake via target_compile_definitions;
// guard the manual define so we never trigger C4005 (macro redefinition).
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <fstream>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <stdexcept>
#include <algorithm>

// Minimal JSON serialisation — no third-party dependency.
// Only handles the flat numeric/bool values stored in AppConfig.
namespace enjoystick::config::json {

    // Tiny key-value parser: returns value string for a given key.
    static std::string GetValue(const std::string& json, const std::string& key) {
        const std::string needle = '"' + key + '"';
        auto pos = json.find(needle);
        if (pos == std::string::npos) return {};
        pos = json.find(':', pos + needle.size());
        if (pos == std::string::npos) return {};
        ++pos;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
        size_t end = pos;
        if (json[end] == '"') {
            ++end;
            while (end < json.size() && json[end] != '"') ++end;
            return json.substr(pos + 1, end - pos - 1);
        }
        while (end < json.size() && json[end] != ',' && json[end] != '}' &&
               json[end] != '\n') ++end;
        std::string val = json.substr(pos, end - pos);
        val.erase(0, val.find_first_not_of(" \t"));
        val.erase(val.find_last_not_of(" \t\r\n") + 1);
        return val;
    }

    static float GetFloat(const std::string& j, const std::string& k, float def) {
        const auto s = GetValue(j, k);
        return s.empty() ? def : std::stof(s);
    }
    static bool GetBool(const std::string& j, const std::string& k, bool def) {
        const auto s = GetValue(j, k);
        return s.empty() ? def : (s == "true");
    }
    static uint32_t GetUint(const std::string& j, const std::string& k, uint32_t def) {
        const auto s = GetValue(j, k);
        return s.empty() ? def : static_cast<uint32_t>(std::stoul(s));
    }

} // namespace enjoystick::config::json

namespace enjoystick::config {

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

class ConfigStoreImpl final : public ConfigStore {
public:
    ConfigStoreImpl() {
        // Build path: %APPDATA%\Enjoystick\config.json
        wchar_t appdata[MAX_PATH];
        ExpandEnvironmentStringsW(L"%APPDATA%", appdata, MAX_PATH);
        m_dir  = std::filesystem::path(appdata) / L"Enjoystick";
        m_path = m_dir / L"config.json";
        std::filesystem::create_directories(m_dir);
    }

    ~ConfigStoreImpl() override { StopWatcher(); }

    void Load() override {
        if (!std::filesystem::exists(m_path)) { Save(); return; }
        std::ifstream f(m_path);
        if (!f) return;
        std::ostringstream ss;
        ss << f.rdbuf();
        const std::string text = ss.str();
        ParseJson(text);
        NotifyChanged();
    }

    void Save() const override {
        std::ofstream f(m_path);
        if (!f) return;
        f << Serialise();
    }

    [[nodiscard]] const AppConfig& Get() const noexcept override { return m_config; }

    void SetCursorConfig(cursor::VirtualMouse::Config cfg) override {
        m_config.cursor = std::move(cfg);
        Save();
        NotifyChanged();
    }

    void SetDeadzoneConfig(core::DeadzoneConfig cfg) override {
        m_config.deadzone = std::move(cfg);
        Save();
        NotifyChanged();
    }

    [[nodiscard]] CallbackHandle OnChanged(ConfigChangedCallback cb) override {
        std::lock_guard lock(m_cbMutex);
        const uint64_t id = ++m_nextId;
        m_callbacks.push_back({id, std::move(cb)});
        return CallbackHandle([this, id] {
            std::lock_guard l(m_cbMutex);
            m_callbacks.erase(
                std::remove_if(m_callbacks.begin(), m_callbacks.end(),
                               [id](const auto& e) { return e.first == id; }),
                m_callbacks.end());
        });
    }

    void StartWatcher() override {
        if (m_watching.exchange(true)) return;
        m_watchThread = std::thread([this] { WatchLoop(); });
    }

    void StopWatcher() override {
        if (!m_watching.exchange(false)) return;
        if (m_watchThread.joinable()) m_watchThread.join();
    }

    [[nodiscard]] std::filesystem::path GetConfigPath() const override { return m_path; }

private:
    // ─── Serialisation ───────────────────────────────────────────────────────

    [[nodiscard]] std::string Serialise() const {
        std::ostringstream o;
        const auto& c = m_config.cursor;
        const auto& d = m_config.deadzone;
        o << "{\n";
        o << "  \"cursor_maxSpeedPx\": "        << c.maxSpeedPx      << ",\n";
        o << "  \"cursor_curveExponent\": "     << c.curveExponent   << ",\n";
        o << "  \"cursor_accelerationMs\": "    << c.accelerationMs  << ",\n";
        o << "  \"cursor_useRightStick\": "     << (c.useRightStick    ? "true" : "false") << ",\n";
        o << "  \"cursor_triggersAsClicks\": "  << (c.triggersAsClicks ? "true" : "false") << ",\n";
        o << "  \"cursor_scrollSpeed\": "       << c.scrollSpeed     << ",\n";
        o << "  \"cursor_invertScroll\": "      << (c.invertScroll    ? "true" : "false") << ",\n";
        o << "  \"dz_innerRadius\": "           << d.innerRadius     << ",\n";
        o << "  \"dz_outerRadius\": "           << d.outerRadius     << ",\n";
        o << "  \"dz_mode\": "                  << static_cast<int>(d.mode) << "\n";
        o << "}\n";
        return o.str();
    }

    void ParseJson(const std::string& j) {
        using namespace json;
        auto& c = m_config.cursor;
        auto& d = m_config.deadzone;
        c.maxSpeedPx       = GetFloat(j, "cursor_maxSpeedPx",       c.maxSpeedPx);
        c.curveExponent    = GetFloat(j, "cursor_curveExponent",    c.curveExponent);
        c.accelerationMs   = GetFloat(j, "cursor_accelerationMs",   c.accelerationMs);
        c.useRightStick    = GetBool (j, "cursor_useRightStick",    c.useRightStick);
        c.triggersAsClicks = GetBool (j, "cursor_triggersAsClicks", c.triggersAsClicks);
        c.scrollSpeed      = GetFloat(j, "cursor_scrollSpeed",      c.scrollSpeed);
        c.invertScroll     = GetBool (j, "cursor_invertScroll",     c.invertScroll);
        d.innerRadius      = GetFloat(j, "dz_innerRadius",          d.innerRadius);
        d.outerRadius      = GetFloat(j, "dz_outerRadius",          d.outerRadius);
        d.mode = static_cast<core::DeadzoneConfig::Mode>(
                     GetUint(j, "dz_mode", static_cast<uint32_t>(d.mode)));
        // Clamp to sane ranges
        c.maxSpeedPx     = std::clamp(c.maxSpeedPx,     100.0f,  10000.0f);
        c.curveExponent  = std::clamp(c.curveExponent,  0.3f,    3.0f);
        c.accelerationMs = std::clamp(c.accelerationMs, 0.0f,    500.0f);
        c.scrollSpeed    = std::clamp(c.scrollSpeed,    1.0f,    50.0f);
        d.innerRadius    = std::clamp(d.innerRadius,    0.0f,    0.5f);
        d.outerRadius    = std::clamp(d.outerRadius,    d.innerRadius + 0.01f, 1.0f);
    }

    void NotifyChanged() {
        std::lock_guard lock(m_cbMutex);
        for (const auto& [id, cb] : m_callbacks) cb(m_config);
    }

    // ─── File watcher ─────────────────────────────────────────────────────────

    void WatchLoop() {
        HANDLE hDir = CreateFileW(
            m_dir.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr);

        if (hDir == INVALID_HANDLE_VALUE) return;

        alignas(DWORD) uint8_t buf[4096];
        OVERLAPPED ov = {};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        while (m_watching.load(std::memory_order_relaxed)) {
            ResetEvent(ov.hEvent);
            DWORD bytesReturned = 0;
            ReadDirectoryChangesW(
                hDir, buf, sizeof(buf), FALSE,
                FILE_NOTIFY_CHANGE_LAST_WRITE,
                &bytesReturned, &ov, nullptr);

            const DWORD wait = WaitForSingleObject(ov.hEvent, 300);
            if (wait == WAIT_OBJECT_0) {
                Sleep(150); // debounce: editor may write in two passes
                Load();
            }
        }

        CloseHandle(ov.hEvent);
        CloseHandle(hDir);
    }

    // ─── Fields ────────────────────────────────────────────────────────────────
    AppConfig              m_config;
    std::filesystem::path  m_dir;
    std::filesystem::path  m_path;

    mutable std::mutex  m_cbMutex;
    uint64_t            m_nextId = 0;
    std::vector<std::pair<uint64_t, ConfigChangedCallback>> m_callbacks;

    std::atomic<bool>   m_watching{false};
    std::thread         m_watchThread;
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::unique_ptr<ConfigStore> ConfigStore::Create() {
    return std::make_unique<ConfigStoreImpl>();
}

} // namespace enjoystick::config
