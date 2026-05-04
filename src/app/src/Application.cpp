#include <enjoystick/app/Application.hpp>
#include <enjoystick/app/SystemTray.hpp>
#include "AutoStart.hpp"

#include <Windows.h>
#include <shellapi.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace enjoystick::app {

// ---------------------------------------------------------------------------
// InputMode
// ---------------------------------------------------------------------------

enum class InputMode : uint8_t {
    Cursor,    ///< Right-stick = mouse; triggers = clicks; LB/RB = scroll mod
    Navigate,  ///< D-pad / face buttons = keyboard shortcuts
};

static const wchar_t* InputModeLabel(InputMode m) noexcept {
    return (m == InputMode::Cursor)
        ? L"\U0001F5B1  Cursor mode"
        : L"\u2B06  Navigate mode";
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

        // Start in Cursor mode — mouse enabled, keyboard mapper disabled
        m_virtualMouse->SetEnabled(true);
        m_keyMapper->SetEnabled(false);

        // Overlay
        m_overlay = overlay::OverlayWindow::Create({});
        SetupRadialMenu();
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

        // Live config propagation
        m_configHandle = m_config->OnChanged([this](const config::AppConfig& c) {
            m_virtualMouse->SetConfig(c.cursor);
        });

        m_inputEngine->Start();

        // Register auto-start silently on first launch
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

    // -----------------------------------------------------------------------
    // ToggleInputMode (public API + internal)
    // -----------------------------------------------------------------------

    void ToggleInputMode() override {
        SetMode(m_mode == InputMode::Cursor ? InputMode::Navigate : InputMode::Cursor);
    }

private:

    void SetMode(InputMode newMode) {
        if (m_mode == newMode) return;
        m_mode = newMode;

        const bool cursor = (m_mode == InputMode::Cursor);
        m_virtualMouse->SetEnabled(cursor);
        m_keyMapper->SetEnabled(!cursor);

        if (m_overlay) m_overlay->ShowToast(InputModeLabel(m_mode));
        if (m_tray)    m_tray->SetMenuItems(BuildTrayMenu());

        // Haptic double-pulse: first controller gets feedback
        if (m_inputEngine) {
            m_inputEngine->Rumble(ControllerId{0}, {0.0f, 0.55f, 80});
            // Second pulse after 120ms via detached thread
            std::thread([this] {
                Sleep(120);
                if (m_inputEngine)
                    m_inputEngine->Rumble(ControllerId{0}, {0.0f, 0.40f, 60});
            }).detach();
        }
    }

    // -----------------------------------------------------------------------
    // OnControllerState — called from polling thread at 250 Hz
    // -----------------------------------------------------------------------

    void OnControllerState(const ControllerState& state) {
        // --- Delta time ------------------------------------------------------
        LARGE_INTEGER freq, now;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&now);
        const float dt = (m_lastTick == 0)
            ? 0.004f
            : static_cast<float>(now.QuadPart - m_lastTick) /
              static_cast<float>(freq.QuadPart);
        m_lastTick = now.QuadPart;

        // --- Button edge detection ------------------------------------------
        // Use locally-tracked previous state so Application handles edges
        // independently of KeyboardMapper (which has its own m_prevButtons).
        const uint32_t prevMask = static_cast<uint32_t>(m_prevButtons);
        const uint32_t currMask = static_cast<uint32_t>(state.buttons);
        const uint32_t downMask = currMask & ~prevMask;   // newly pressed
        m_prevButtons = state.buttons;

        auto pressed = [&](Button b) -> bool {
            return (downMask & static_cast<uint32_t>(b)) != 0;
        };
        auto held = [&](Button b) -> bool {
            return (currMask & static_cast<uint32_t>(b)) != 0;
        };

        // --- LB + RB chord: instant mode toggle (no menu needed) ------------
        if (held(Button::LB) && held(Button::RB)) {
            if (!m_lbRbChordActive) {
                m_lbRbChordActive = true;
                ToggleInputMode();
            }
        } else {
            m_lbRbChordActive = false;
        }

        // --- Guide button: open/close radial menu ---------------------------
        // Guide + LT  = force Cursor mode
        // Guide + RT  = force Navigate mode
        // Guide alone = toggle radial menu
        if (pressed(Button::Guide)) {
            if (held(Button::LT_Click)) {
                SetMode(InputMode::Cursor);
            } else if (held(Button::RT_Click)) {
                SetMode(InputMode::Navigate);
            } else {
                auto& rm = m_overlay->GetRadialMenu();
                if (rm.IsVisible()) rm.Close();
                else                rm.Open();
            }
        }

        // --- Select (Back): single-press mode toggle -------------------------
        if (pressed(Button::Select)) {
            ToggleInputMode();
        }

        // --- Delegate to subsystems -----------------------------------------
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
    // Radial menu setup
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
                []{ ShellExecuteW(nullptr, L"open", L"ms-settings:",
                                  nullptr, nullptr, SW_SHOW); } },
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
            { L"Open Settings",
              []{ ShellExecuteW(nullptr, L"open", L"ms-settings:",
                                nullptr, nullptr, SW_SHOW); } },
            { autoLabel,
              [autoOn]{
                  if (autoOn) AutoStart::Disable();
                  else        AutoStart::Enable();
              } },
            { L"",                {},       true  },
            { L"Exit",            [this]{ Exit(); } },
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

    InputMode m_mode         = InputMode::Cursor;
    int64_t   m_lastTick     = 0;
    Button    m_prevButtons  = Button::None;
    bool      m_lbRbChordActive = false;
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::unique_ptr<Application> Application::Create() {
    return std::make_unique<ApplicationImpl>();
}

} // namespace enjoystick::app
