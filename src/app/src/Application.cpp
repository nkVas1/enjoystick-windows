#include <enjoystick/app/Application.hpp>
#include <enjoystick/app/SystemTray.hpp>
#include <enjoystick/config/ConfigStore.hpp>
#include "AutoStart.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shlobj.h>
#include <shellapi.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace enjoystick::app {

enum class InputMode : uint8_t {
    Cursor,
    Navigate,
};

static const wchar_t* InputModeLabel(InputMode m) noexcept {
    return (m == InputMode::Cursor)
        ? L"\U0001F5B1  Cursor mode"
        : L"\u2B06  Navigate mode";
}

// Returns a human-readable label for the controller type.
static const wchar_t* ControllerTypeLabel(ControllerType t) noexcept {
    switch (t) {
        case ControllerType::PlayStation: return L"PlayStation";
        case ControllerType::Xbox:        return L"Xbox";
        case ControllerType::Generic:     return L"Gamepad";
        default:                          return L"Controller";
    }
}

namespace {

struct RumbleCtx {
    core::InputEngine* engine;
    ControllerId       id;
    RumbleParams       params;
};

static void CALLBACK RumbleTimerCallback(
    PTP_CALLBACK_INSTANCE, void* ctx, PTP_TIMER timer) noexcept
{
    auto* r = static_cast<RumbleCtx*>(ctx);
    if (r->engine) r->engine->Rumble(r->id, r->params);
    CloseThreadpoolTimer(timer);
    delete r;
}

static void ScheduleRumble(
    core::InputEngine* engine,
    ControllerId id,
    RumbleParams params,
    DWORD deferredMs) noexcept
{
    auto* ctx  = new (std::nothrow) RumbleCtx{engine, id, params};
    if (!ctx) return;
    PTP_TIMER timer = CreateThreadpoolTimer(RumbleTimerCallback, ctx, nullptr);
    if (!timer) { delete ctx; return; }
    ULARGE_INTEGER due;
    due.QuadPart = static_cast<ULONGLONG>(
        -static_cast<LONGLONG>(deferredMs) * 10'000LL);
    FILETIME ft;
    ft.dwLowDateTime  = due.LowPart;
    ft.dwHighDateTime = due.HighPart;
    SetThreadpoolTimer(timer, &ft, 0, 0);
}

static void SendScrollLines(int lines) noexcept {
    INPUT inp{};
    inp.type         = INPUT_MOUSE;
    inp.mi.dwFlags   = MOUSEEVENTF_WHEEL;
    inp.mi.mouseData = static_cast<DWORD>(lines * WHEEL_DELTA);
    SendInput(1, &inp, sizeof(INPUT));
}

static void SendXButton(DWORD which, bool down) noexcept {
    INPUT inp{};
    inp.type         = INPUT_MOUSE;
    inp.mi.dwFlags   = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
    inp.mi.mouseData = which;
    SendInput(1, &inp, sizeof(INPUT));
}

static void SendKey(WORD vk, bool down, bool extended = false) noexcept {
    INPUT inp{};
    inp.type       = INPUT_KEYBOARD;
    inp.ki.wVk     = vk;
    inp.ki.dwFlags = (down ? 0u : KEYEVENTF_KEYUP) |
                     (extended ? KEYEVENTF_EXTENDEDKEY : 0u);
    SendInput(1, &inp, sizeof(INPUT));
}

static void SendBrowserTab(bool forward) noexcept {
    if (!forward) SendKey(VK_SHIFT, true);
    SendKey(VK_CONTROL, true);
    SendKey(VK_TAB, true);
    SendKey(VK_TAB, false);
    SendKey(VK_CONTROL, false);
    if (!forward) SendKey(VK_SHIFT, false);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Helpers: translate between config/UI value bags and MouseConfig
// ---------------------------------------------------------------------------

static cursor::MouseConfig VMConfigFromSettings(
    const overlay::SettingsMenu::Values& v) noexcept
{
    cursor::MouseConfig c;
    c.maxSpeedPx         = v.cursorSpeed;
    c.curveExponent      = v.curveExponent;
    c.accelerationMs     = v.accelerationMs;
    c.scrollSpeed        = v.scrollSpeed;
    c.triggersAsClicks   = v.triggersAsClicks;
    c.useRightStick      = v.useRightStick;
    c.adaptiveSpeed      = v.adaptiveSpeed;
    c.targetTraversalMs  = v.targetTraversalMs;
    c.dpiWeight          = v.dpiWeight;
    c.adaptiveMinScale   = 0.30f;
    c.adaptiveMaxScale   = 2.50f;
    return c;
}

static overlay::SettingsMenu::Values SettingsValuesFromConfig(
    const config::MouseCfg&  mc,
    const config::InputCfg&  ic) noexcept
{
    overlay::SettingsMenu::Values v;
    v.cursorSpeed        = mc.maxSpeed;
    v.curveExponent      = mc.exponent;
    v.accelerationMs     = mc.accelerationMs;
    v.scrollSpeed        = mc.scrollSpeed;
    v.triggersAsClicks   = mc.triggersAsClicks;
    v.useRightStick      = mc.useRightStick;
    v.adaptiveSpeed      = mc.adaptiveSpeed;
    v.targetTraversalMs  = mc.targetTraversalMs;
    v.dpiWeight          = mc.dpiWeight;
    v.dzInner            = ic.deadzoneInner;
    v.dzOuter            = ic.deadzoneOuter;
    return v;
}

class ApplicationImpl final : public Application {
public:
    ApplicationImpl() {
        QueryPerformanceFrequency(&m_qpcFreq);
    }
    ~ApplicationImpl() override = default;

    void Init() override {
        m_config = config::ConfigStore::Create();
        m_config->StartWatcher();

        core::InputEngine::Config engCfg;
        engCfg.pollingRateHz  = 250;
        engCfg.hapticsEnabled = true;
        m_inputEngine = core::InputEngine::Create(engCfg);

        const auto& cfg = m_config->Get();
        cursor::MouseConfig vmCfg;
        vmCfg.maxSpeedPx        = cfg.mouse.maxSpeed;
        vmCfg.curveExponent     = cfg.mouse.exponent;
        vmCfg.linearZone        = cfg.mouse.linearZone;
        vmCfg.scrollSpeed       = cfg.mouse.scrollSpeed;
        vmCfg.wrapEdges         = cfg.mouse.wrapEdges;
        vmCfg.triggersAsClicks  = cfg.mouse.triggersAsClicks;
        vmCfg.useRightStick     = cfg.mouse.useRightStick;
        vmCfg.accelerationMs    = cfg.mouse.accelerationMs;
        vmCfg.adaptiveSpeed     = cfg.mouse.adaptiveSpeed;
        vmCfg.targetTraversalMs = cfg.mouse.targetTraversalMs;
        vmCfg.dpiWeight         = cfg.mouse.dpiWeight;
        vmCfg.adaptiveMinScale  = cfg.mouse.adaptiveMinScale;
        vmCfg.adaptiveMaxScale  = cfg.mouse.adaptiveMaxScale;
        m_virtualMouse = std::make_unique<cursor::VirtualMouse>(vmCfg);
        m_keyMapper    = std::make_unique<input::KeyboardMapper>();

        m_virtualMouse->SetEnabled(true);
        m_keyMapper->SetEnabled(false);

        m_overlay = overlay::OverlayWindow::Create({});
        SetupRadialMenu();
        SetupSettingsMenu();
        m_overlay->Show();
        m_overlay->SetModeLabel(InputModeLabel(m_mode));

        m_tray = SystemTray::Create(L"EnjoyStick \u2014 gamepad navigation active");
        m_tray->SetMenuItems(BuildTrayMenu());
        // Double-click on the tray icon opens Settings (same as right-click → Open Settings).
        m_tray->SetOnDoubleClick([this] { OpenSettingsMenu(); });

        m_inputHandle = m_inputEngine->OnInput([this](const ControllerState& s) {
            OnControllerState(s);
        });
        m_connHandle = m_inputEngine->OnConnection([this](ControllerId id, ConnectionEvent ev) {
            OnConnectionEvent(id, ev);
        });

        m_configHandle = m_config->OnChanged([this](const config::Config& c) {
            cursor::MouseConfig mc;
            mc.maxSpeedPx        = c.mouse.maxSpeed;
            mc.curveExponent     = c.mouse.exponent;
            mc.linearZone        = c.mouse.linearZone;
            mc.scrollSpeed       = c.mouse.scrollSpeed;
            mc.wrapEdges         = c.mouse.wrapEdges;
            mc.triggersAsClicks  = c.mouse.triggersAsClicks;
            mc.useRightStick     = c.mouse.useRightStick;
            mc.accelerationMs    = c.mouse.accelerationMs;
            mc.adaptiveSpeed     = c.mouse.adaptiveSpeed;
            mc.targetTraversalMs = c.mouse.targetTraversalMs;
            mc.dpiWeight         = c.mouse.dpiWeight;
            mc.adaptiveMinScale  = c.mouse.adaptiveMinScale;
            mc.adaptiveMaxScale  = c.mouse.adaptiveMaxScale;
            m_virtualMouse->SetConfig(mc);
        });

        m_inputEngine->Start();

        if (!AutoStart::IsEnabled()) AutoStart::Enable();
    }

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
    void SetupRadialMenu() {
        using RM = overlay::RadialMenuItem;
        m_overlay->GetRadialMenu().SetItems({
            RM{ L"Desktop",  L"\U0001F5A5", []{ ShellExecuteW(nullptr, L"open", L"shell:Desktop", nullptr, nullptr, SW_SHOW); } },
            RM{ L"Files",    L"\U0001F4C2", []{ ShellExecuteW(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOW); } },
            RM{ L"Settings", L"\u2699", [this]{ OpenSettingsMenu(); } },
            RM{ L"Search",   L"\U0001F50D", []{ keybd_event(VK_LWIN, 0, 0, 0); keybd_event('S', 0, 0, 0); keybd_event('S', 0, KEYEVENTF_KEYUP, 0); keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0); } },
            RM{ L"Media",    L"\U0001F3B5", []{ keybd_event(VK_MEDIA_PLAY_PAUSE, 0, 0, 0); keybd_event(VK_MEDIA_PLAY_PAUSE, 0, KEYEVENTF_KEYUP, 0); } },
            RM{ L"Mode",     L"\U0001F501", [this]{ ToggleInputMode(); } },
            RM{ L"Exit",     L"\u23F9",     [this]{ Exit(); } },
        });

        m_overlay->GetRadialMenu().SetOnOpen([this] {
            m_virtualMouse->SetEnabled(false);
        });
        m_overlay->GetRadialMenu().SetOnClose([this] {
            if (m_mode == InputMode::Cursor)
                m_virtualMouse->SetEnabled(true);
        });
    }

    void SetupSettingsMenu() {
        m_overlay->GetSettingsMenu().SetOnChanged(
            [this](const overlay::SettingsMenu::Values& v) {
                const auto vmCfg = VMConfigFromSettings(v);
                m_virtualMouse->SetConfig(vmCfg);
                m_config->SetCursorConfig(vmCfg);

                core::DeadzoneConfig dz;
                dz.innerRadius = v.dzInner;
                dz.outerRadius = v.dzOuter;
                m_config->SetDeadzoneConfig(dz);

                if (m_inputEngine)
                    m_inputEngine->Rumble(ControllerId{0}, {0.0f, 0.20f, 30});
            }
        );
    }

    void OpenSettingsMenu() {
        const auto& cfg  = m_config->Get();
        const auto  vals = SettingsValuesFromConfig(cfg.mouse, cfg.input);
        m_overlay->GetSettingsMenu().Open(vals);
        m_overlay->GetRadialMenu().Close();
    }

    void SetMode(InputMode newMode) {
        if (m_mode == newMode) return;
        m_mode = newMode;

        const bool cursor   = (m_mode == InputMode::Cursor);
        const bool menuOpen = m_overlay->GetRadialMenu().IsVisible();
        m_virtualMouse->SetEnabled(cursor && !menuOpen);
        m_keyMapper->SetEnabled(!cursor);

        if (m_overlay) {
            m_overlay->SetModeLabel(InputModeLabel(m_mode));
            m_overlay->ShowToast(InputModeLabel(m_mode));
        }
        if (m_tray) m_tray->SetMenuItems(BuildTrayMenu());

        if (m_inputEngine) {
            m_inputEngine->Rumble(ControllerId{0}, {0.0f, 0.55f, 80});
            ScheduleRumble(m_inputEngine.get(), ControllerId{0}, {0.0f, 0.40f, 60}, 120);
        }
    }

    void OnControllerState(const ControllerState& state) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const float dt = (m_lastTick == 0)
            ? 4.0f
            : static_cast<float>(now.QuadPart - m_lastTick) * 1000.0f /
              static_cast<float>(m_qpcFreq.QuadPart);
        m_lastTick = now.QuadPart;

        const uint32_t prevMask = static_cast<uint32_t>(m_prevButtons);
        const uint32_t currMask = static_cast<uint32_t>(state.buttons);
        const uint32_t downMask = currMask & ~prevMask;
        const uint32_t upMask   = prevMask & ~currMask;
        m_prevButtons = state.buttons;

        auto pressed = [&](Button b) -> bool {
            return (downMask & static_cast<uint32_t>(b)) != 0;
        };
        auto released = [&](Button b) -> bool {
            return (upMask & static_cast<uint32_t>(b)) != 0;
        };
        auto held = [&](Button b) -> bool {
            return (currMask & static_cast<uint32_t>(b)) != 0;
        };

        const bool radialOpen   = m_overlay->GetRadialMenu().IsVisible();
        const bool settingsOpen = m_overlay->GetSettingsMenu().IsOpen();

        if (pressed(Button::Guide)) {
            if (settingsOpen) {
                m_overlay->GetSettingsMenu().Close();
                return;
            }
            if (held(Button::Select)) {
                auto& sm = m_overlay->GetSettingsMenu();
                if (!radialOpen) {
                    if (sm.IsOpen()) sm.Close();
                    else             OpenSettingsMenu();
                }
            } else {
                auto& rm = m_overlay->GetRadialMenu();
                if (rm.IsVisible()) rm.Close();
                else                rm.Open();
            }
            return;
        }

        if (radialOpen) {
            m_overlay->GetRadialMenu().Update(state, dt * 0.001f);
            return;
        }

        if (settingsOpen) {
            m_overlay->GetSettingsMenu().Update(state, dt * 0.001f);
            return;
        }

        if (held(Button::LB) && held(Button::RB)) {
            if (!m_lbRbChordActive) {
                m_lbRbChordActive = true;
                ToggleInputMode();
            }
        } else {
            m_lbRbChordActive = false;
        }

        if (m_mode == InputMode::Cursor) {
            if (!held(Button::LB) || !held(Button::RB)) {
                if (pressed(Button::LB)) { SendBrowserTab(false); }
                if (pressed(Button::RB)) { SendBrowserTab(true);  }
            }

            if (pressed(Button::North)) m_virtualMouse->MiddleClick();

            if (pressed(Button::West)) {
                SendXButton(XBUTTON1, true);
                SendXButton(XBUTTON1, false);
            }

            if (pressed(Button::DPadLeft)) {
                SendKey(VK_MENU,  true,  true);
                SendKey(VK_LEFT,  true,  true);
                SendKey(VK_LEFT,  false, true);
                SendKey(VK_MENU,  false, true);
            }
            if (pressed(Button::DPadRight)) {
                SendKey(VK_MENU,  true,  true);
                SendKey(VK_RIGHT, true,  true);
                SendKey(VK_RIGHT, false, true);
                SendKey(VK_MENU,  false, true);
            }
            if (pressed(Button::DPadUp))   SendScrollLines(+1);
            if (pressed(Button::DPadDown)) SendScrollLines(-1);

            if (pressed(Button::Select)) {
                SendKey(VK_LWIN, true);
                SendKey(VK_TAB,  true,  true);
                SendKey(VK_TAB,  false, true);
                SendKey(VK_LWIN, false);
            }

            if (pressed(Button::LS)) {
                m_virtualMouse->LeftClick();
                m_virtualMouse->LeftClick();
            }

            if (held(Button::East)) {
                m_eastHoldMs += dt;
                if (m_eastHoldMs >= kEastLongPressMs && !m_eastLongActive) {
                    m_eastLongActive = true;
                    m_virtualMouse->LeftDown();
                }
            }
            if (released(Button::East)) {
                if (m_eastLongActive) {
                    m_virtualMouse->LeftUp();
                } else if (m_eastHoldMs < kEastLongPressMs && m_eastHoldMs > 0.0f) {
                    m_virtualMouse->RightClick();
                }
                m_eastHoldMs     = 0.0f;
                m_eastLongActive = false;
            }
        }

        m_virtualMouse->Update(state, dt);
        m_keyMapper->Update(state);
        m_overlay->PostState(state);
    }

    void OnConnectionEvent(ControllerId id, ConnectionEvent ev) {
        const bool connected = (ev == ConnectionEvent::Connected);

        // Determine the controller type label for a richer notification.
        // GetConnectedControllers() is live; on disconnect the list may already
        // be cleared, so we default to a generic label.
        std::wstring typeLabel;
        if (m_inputEngine) {
            const auto list = m_inputEngine->GetConnectedControllers();
            for (const auto& info : list) {
                if (info.id == id) {
                    typeLabel = ControllerTypeLabel(info.type);
                    break;
                }
            }
        }
        if (typeLabel.empty()) typeLabel = L"Controller";

        const std::wstring msg = connected
            ? (L"\U0001F3AE  " + typeLabel + L" connected")
            : (L"\u26A0  "     + typeLabel + L" disconnected");

        if (m_overlay) m_overlay->ShowToast(msg);
        if (m_tray)    m_tray->ShowBalloon(L"EnjoyStick", msg);
        if (connected && m_inputEngine)
            m_inputEngine->Rumble(id, {0.3f, 0.6f, 150});
    }

    std::vector<TrayMenuItem> BuildTrayMenu() {
        const wchar_t* modeLabel = (m_mode == InputMode::Cursor)
            ? L"Switch to Navigate mode"
            : L"Switch to Cursor mode";
        const bool autoOn = AutoStart::IsEnabled();
        const wchar_t* autoLabel = autoOn ? L"\u2713 Launch on login" : L"  Launch on login";
        return {
            { L"EnjoyStick v0.1", {},       false },
            { L"",                {},       true  },
            { modeLabel,          [this]{ ToggleInputMode(); } },
            { L"Open Settings",   [this]{ OpenSettingsMenu(); } },
            { autoLabel, [autoOn]{ if (autoOn) AutoStart::Disable(); else AutoStart::Enable(); } },
            { L"",     {},       true },
            { L"Exit", [this]{ Exit(); } },
        };
    }

    std::unique_ptr<config::ConfigStore>    m_config;
    std::unique_ptr<core::InputEngine>      m_inputEngine;
    std::unique_ptr<cursor::VirtualMouse>   m_virtualMouse;
    std::unique_ptr<input::KeyboardMapper>  m_keyMapper;
    std::unique_ptr<overlay::OverlayWindow> m_overlay;
    std::unique_ptr<SystemTray>             m_tray;

    CallbackHandle               m_inputHandle;
    CallbackHandle               m_connHandle;
    config::ConfigCallbackHandle m_configHandle;

    InputMode      m_mode            = InputMode::Cursor;
    int64_t        m_lastTick        = 0;
    LARGE_INTEGER  m_qpcFreq         = {};
    Button         m_prevButtons     = Button::None;
    bool           m_lbRbChordActive = false;

    static constexpr float kEastLongPressMs = 600.0f;
    float  m_eastHoldMs     = 0.0f;
    bool   m_eastLongActive = false;
};

std::unique_ptr<Application> Application::Create() {
    return std::make_unique<ApplicationImpl>();
}

} // namespace enjoystick::app
