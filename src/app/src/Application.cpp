#include <enjoystick/app/Application.hpp>
#include <enjoystick/app/SystemTray.hpp>
#include "AutoStart.hpp"

#include <Windows.h>
#include <shellapi.h>

#include <stdexcept>
#include <string>
#include <vector>
#include <thread>

namespace enjoystick::app {

// ---------------------------------------------------------------------------
// InputMode
// ---------------------------------------------------------------------------

enum class InputMode : uint8_t {
    Cursor,    ///< Right-stick = mouse; triggers = clicks
    Navigate,  ///< D-pad / face buttons = keyboard shortcuts
};

static const wchar_t* InputModeLabel(InputMode m) noexcept {
    return (m == InputMode::Cursor)
        ? L"\U0001F5B1  Cursor mode"
        : L"\u2B06  Navigate mode";
}

// ---------------------------------------------------------------------------
// Helper: convert SettingsMenu::Values -> VirtualMouse::Config
// ---------------------------------------------------------------------------

static cursor::VirtualMouse::Config VMConfigFromSettings(
    const overlay::SettingsMenu::Values& v) noexcept
{
    cursor::VirtualMouse::Config c;
    c.maxSpeedPx        = v.cursorSpeed;
    c.curveExponent     = v.curveExponent;
    c.accelerationMs    = v.accelerationMs;
    c.scrollSpeed       = v.scrollSpeed;
    c.triggersAsClicks  = v.triggersAsClicks;
    c.useRightStick     = v.useRightStick;
    return c;
}

// ---------------------------------------------------------------------------
// Helper: convert VirtualMouse::Config + DeadzoneConfig -> SettingsMenu::Values
// ---------------------------------------------------------------------------

static overlay::SettingsMenu::Values SettingsValuesFromConfig(
    const cursor::VirtualMouse::Config& c,
    const core::DeadzoneConfig&         dz) noexcept
{
    overlay::SettingsMenu::Values v;
    v.cursorSpeed      = c.maxSpeedPx;
    v.curveExponent    = c.curveExponent;
    v.accelerationMs   = c.accelerationMs;
    v.scrollSpeed      = c.scrollSpeed;
    v.triggersAsClicks = c.triggersAsClicks;
    v.useRightStick    = c.useRightStick;
    v.dzInner          = dz.innerRadius;   // was dz.inner — field is innerRadius
    v.dzOuter          = dz.outerRadius;   // was dz.outer — field is outerRadius
    return v;
}

// ---------------------------------------------------------------------------
// ApplicationImpl
// ---------------------------------------------------------------------------

class ApplicationImpl final : public Application {
public:
    ApplicationImpl() = default;
    ~ApplicationImpl() override = default;

    // -----------------------------------------------------------------------
    // Init
    // -----------------------------------------------------------------------

    void Init() override {
        // Config
        m_config = config::ConfigStore::Create();
        m_config->Load();
        m_config->StartWatcher();

        // Input engine
        core::InputEngine::Config engCfg;
        engCfg.pollingRateHz  = 250;
        engCfg.hapticsEnabled = true;
        m_inputEngine = core::InputEngine::Create(engCfg);

        // Subsystems
        const auto& cfg = m_config->Get();
        m_virtualMouse = std::make_unique<cursor::VirtualMouse>(cfg.cursor);
        m_keyMapper    = std::make_unique<input::KeyboardMapper>();

        // Start in Cursor mode
        m_virtualMouse->SetEnabled(true);
        m_keyMapper->SetEnabled(false);

        // Overlay
        m_overlay = overlay::OverlayWindow::Create({});
        SetupRadialMenu();
        SetupSettingsMenu();
        m_overlay->Show();

        // System tray
        m_tray = SystemTray::Create(L"EnjoyStick \u2014 gamepad navigation active");
        m_tray->SetMenuItems(BuildTrayMenu());

        // Input callbacks
        m_inputHandle = m_inputEngine->OnInput([this](const ControllerState& s) {
            OnControllerState(s);
        });
        m_connHandle = m_inputEngine->OnConnection([this](ControllerId id, ConnectionEvent ev) {
            OnConnectionEvent(id, ev);
        });

        // Live config propagation (hot-reload from file watcher)
        m_configHandle = m_config->OnChanged([this](const config::AppConfig& c) {
            m_virtualMouse->SetConfig(c.cursor);
        });

        m_inputEngine->Start();

        if (!AutoStart::IsEnabled()) AutoStart::Enable();
    }

    // -----------------------------------------------------------------------
    // Run / Exit
    // -----------------------------------------------------------------------

    int Run() override {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (m_inputEngine) m_inputEngine->Stop();
        if (m_overlay)     m_overlay->Hide();
        if (m_tray)        m_tray->Remove();
        if (m_config)      m_config->StopWatcher();
        return static_cast<int>(msg.wParam);
    }

    void Exit() override { PostQuitMessage(0); }

    void ToggleInputMode() override {
        SetMode(m_mode == InputMode::Cursor ? InputMode::Navigate : InputMode::Cursor);
    }

private:

    // -----------------------------------------------------------------------
    // SetupRadialMenu
    // -----------------------------------------------------------------------

    void SetupRadialMenu() {
        using RM = overlay::RadialMenuItem;
        m_overlay->GetRadialMenu().SetItems({
            RM{ L"Desktop",  L"\U0001F5A5",
                []{ ShellExecuteW(nullptr, L"open", L"shell:Desktop",
                                  nullptr, nullptr, SW_SHOW); } },
            RM{ L"Files",    L"\U0001F4C2",
                []{ ShellExecuteW(nullptr, L"open", L"explorer.exe",
                                  nullptr, nullptr, SW_SHOW); } },
            RM{ L"Settings", L"\u2699",
                [this]{ OpenSettingsMenu(); } },
            RM{ L"Search",   L"\U0001F50D",
                []{ keybd_event(VK_LWIN, 0, 0, 0);
                    keybd_event('S', 0, 0, 0);
                    keybd_event('S', 0, KEYEVENTF_KEYUP, 0);
                    keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0); } },
            RM{ L"Media",    L"\U0001F3B5",
                []{ keybd_event(VK_MEDIA_PLAY_PAUSE, 0, 0, 0);
                    keybd_event(VK_MEDIA_PLAY_PAUSE, 0, KEYEVENTF_KEYUP, 0); } },
            RM{ L"Mode",     L"\U0001F501", [this]{ ToggleInputMode(); } },
            RM{ L"Exit",     L"\u23F9",     [this]{ Exit(); } },
        });
    }

    // -----------------------------------------------------------------------
    // SetupSettingsMenu
    // -----------------------------------------------------------------------

    void SetupSettingsMenu() {
        m_overlay->GetSettingsMenu() = overlay::SettingsMenu(
            [this](const overlay::SettingsMenu::Values& v) {
                // Update cursor config
                const auto vmCfg = VMConfigFromSettings(v);
                m_config->SetCursorConfig(vmCfg);
                m_virtualMouse->SetConfig(vmCfg);

                // Update deadzone config
                core::DeadzoneConfig dz;
                dz.innerRadius = v.dzInner;   // was dz.inner
                dz.outerRadius = v.dzOuter;   // was dz.outer
                m_config->SetDeadzoneConfig(dz);

                // Short haptic confirmation
                if (m_inputEngine)
                    m_inputEngine->Rumble(ControllerId{0}, {0.0f, 0.20f, 30});
            }
        );
    }

    // -----------------------------------------------------------------------
    // OpenSettingsMenu
    // -----------------------------------------------------------------------

    void OpenSettingsMenu() {
        const auto& cfg = m_config->Get();
        const auto vals = SettingsValuesFromConfig(cfg.cursor, cfg.deadzone);
        m_overlay->GetSettingsMenu().Open(vals);
        m_overlay->GetRadialMenu().Close();
    }

    // -----------------------------------------------------------------------
    // SetMode
    // -----------------------------------------------------------------------

    void SetMode(InputMode newMode) {
        if (m_mode == newMode) return;
        m_mode = newMode;

        const bool cursor = (m_mode == InputMode::Cursor);
        m_virtualMouse->SetEnabled(cursor);
        m_keyMapper->SetEnabled(!cursor);

        if (m_overlay) m_overlay->ShowToast(InputModeLabel(m_mode));
        if (m_tray)    m_tray->SetMenuItems(BuildTrayMenu());

        // Haptic double-pulse
        if (m_inputEngine) {
            m_inputEngine->Rumble(ControllerId{0}, {0.0f, 0.55f, 80});
            std::thread([this] {
                Sleep(120);
                if (m_inputEngine)
                    m_inputEngine->Rumble(ControllerId{0}, {0.0f, 0.40f, 60});
            }).detach();
        }
    }

    // -----------------------------------------------------------------------
    // OnControllerState (250 Hz polling thread)
    // -----------------------------------------------------------------------

    void OnControllerState(const ControllerState& state) {
        LARGE_INTEGER freq, now;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&now);
        const float dt = (m_lastTick == 0)
            ? 0.004f
            : static_cast<float>(now.QuadPart - m_lastTick) /
              static_cast<float>(freq.QuadPart);
        m_lastTick = now.QuadPart;

        const uint32_t prevMask = static_cast<uint32_t>(m_prevButtons);
        const uint32_t currMask = static_cast<uint32_t>(state.buttons);
        const uint32_t downMask = currMask & ~prevMask;
        m_prevButtons = state.buttons;

        auto pressed = [&](Button b) -> bool {
            return (downMask & static_cast<uint32_t>(b)) != 0;
        };
        auto held = [&](Button b) -> bool {
            return (currMask & static_cast<uint32_t>(b)) != 0;
        };

        // LB + RB chord: mode toggle
        if (held(Button::LB) && held(Button::RB)) {
            if (!m_lbRbChordActive) {
                m_lbRbChordActive = true;
                ToggleInputMode();
            }
        } else {
            m_lbRbChordActive = false;
        }

        // Guide button combos
        if (pressed(Button::Guide)) {
            if (held(Button::LT_Click)) {
                SetMode(InputMode::Cursor);
            } else if (held(Button::RT_Click)) {
                SetMode(InputMode::Navigate);
            } else if (held(Button::Select)) {
                auto& sm = m_overlay->GetSettingsMenu();
                if (sm.IsOpen()) sm.Close();
                else             OpenSettingsMenu();
            } else {
                if (!m_overlay->GetSettingsMenu().IsOpen()) {
                    auto& rm = m_overlay->GetRadialMenu();
                    if (rm.IsVisible()) rm.Close();
                    else                rm.Open();
                }
            }
        }

        // Delegate to subsystems
        m_virtualMouse->Update(state, dt);
        m_keyMapper->Update(state);
        m_overlay->PostState(state);
    }

    // -----------------------------------------------------------------------
    // OnConnectionEvent
    // -----------------------------------------------------------------------

    void OnConnectionEvent(ControllerId id, ConnectionEvent ev) {
        const bool connected = (ev == ConnectionEvent::Connected);
        const std::wstring msg = connected
            ? L"\U0001F3AE  Controller connected"
            : L"\u26A0  Controller disconnected";
        if (m_overlay) m_overlay->ShowToast(msg);
        if (m_tray)    m_tray->ShowBalloon(L"EnjoyStick", msg);
        if (connected && m_inputEngine)
            m_inputEngine->Rumble(id, {0.3f, 0.6f, 150});
    }

    // -----------------------------------------------------------------------
    // Tray menu
    // -----------------------------------------------------------------------

    std::vector<TrayMenuItem> BuildTrayMenu() {
        const wchar_t* modeLabel = (m_mode == InputMode::Cursor)
            ? L"Switch to Navigate mode"
            : L"Switch to Cursor mode";

        const bool autoOn = AutoStart::IsEnabled();
        const wchar_t* autoLabel = autoOn
            ? L"\u2713 Launch on login"
            : L"  Launch on login";

        return {
            { L"EnjoyStick v0.1", {},       false },
            { L"",                {},       true  },
            { modeLabel,          [this]{ ToggleInputMode(); } },
            { L"Open Settings",   [this]{ OpenSettingsMenu(); } },
            { autoLabel,
              [autoOn]{
                  if (autoOn) AutoStart::Disable();
                  else        AutoStart::Enable();
              } },
            { L"",   {},       true },
            { L"Exit", [this]{ Exit(); } },
        };
    }

    // -----------------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------------
    std::unique_ptr<config::ConfigStore>    m_config;
    std::unique_ptr<core::InputEngine>      m_inputEngine;
    std::unique_ptr<cursor::VirtualMouse>   m_virtualMouse;
    std::unique_ptr<input::KeyboardMapper>  m_keyMapper;
    std::unique_ptr<overlay::OverlayWindow> m_overlay;
    std::unique_ptr<SystemTray>             m_tray;

    CallbackHandle m_inputHandle;
    CallbackHandle m_connHandle;
    CallbackHandle m_configHandle;

    InputMode m_mode            = InputMode::Cursor;
    int64_t   m_lastTick        = 0;
    Button    m_prevButtons     = Button::None;
    bool      m_lbRbChordActive = false;
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::unique_ptr<Application> Application::Create() {
    return std::make_unique<ApplicationImpl>();
}

} // namespace enjoystick::app
