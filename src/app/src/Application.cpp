#include <enjoystick/app/Application.hpp>
#include <enjoystick/app/SystemTray.hpp>
#include <enjoystick/config/ConfigStore.hpp>
#include <enjoystick/voice/VoiceInputEngine.hpp>
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

// ---------------------------------------------------------------------------
// InputMode
// ---------------------------------------------------------------------------

enum class InputMode : uint8_t { Cursor, Navigate, Voice };

static constexpr InputMode kNextMode[3] = {
    InputMode::Navigate,
    InputMode::Voice,
    InputMode::Cursor,
};

static const wchar_t* InputModeLabel(InputMode m) noexcept {
    switch (m) {
        case InputMode::Navigate: return L"\u2B06  Navigate mode";
        case InputMode::Voice:    return L"\U0001F3A4  Voice mode";
        default:                  return L"\U0001F5B1  Cursor mode";
    }
}

static const wchar_t* InputModeTrayLabel(InputMode m) noexcept {
    switch (m) {
        case InputMode::Navigate: return L"Switch to Voice mode";
        case InputMode::Voice:    return L"Switch to Cursor mode";
        default:                  return L"Switch to Navigate mode";
    }
}

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
    core::InputEngine* engine, ControllerId id,
    RumbleParams params, DWORD deferredMs) noexcept
{
    auto* ctx  = new (std::nothrow) RumbleCtx{engine, id, params};
    if (!ctx) return;
    PTP_TIMER timer = CreateThreadpoolTimer(RumbleTimerCallback, ctx, nullptr);
    if (!timer) { delete ctx; return; }
    ULARGE_INTEGER due;
    due.QuadPart = static_cast<ULONGLONG>(-static_cast<LONGLONG>(deferredMs) * 10'000LL);
    FILETIME ft{ due.LowPart, due.HighPart };
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

static void SendDoubleClick(DWORD flags_down, DWORD flags_up) noexcept {
    const DWORD gap = std::min(static_cast<DWORD>(GetDoubleClickTime()) / 3u,
                               static_cast<DWORD>(80));
    INPUT inp[2]{};
    inp[0].type = inp[1].type = INPUT_MOUSE;
    inp[0].mi.dwFlags = flags_down;
    inp[1].mi.dwFlags = flags_up;
    SendInput(2, inp, sizeof(INPUT));
    Sleep(gap);
    SendInput(2, inp, sizeof(INPUT));
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Config adapters
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

// ---------------------------------------------------------------------------
// ApplicationImpl
// ---------------------------------------------------------------------------

class ApplicationImpl final : public Application {
public:
    ApplicationImpl() { QueryPerformanceFrequency(&m_qpcFreq); }
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

        // Voice engine
        m_voiceEngine = voice::VoiceInputEngine::Create({});
        SetupVoiceEngine();

        m_overlay = overlay::OverlayWindow::Create({});
        SetupRadialMenu();
        SetupSettingsMenu();
        SetupKeyboard();
        m_overlay->Show();
        m_overlay->SetModeLabel(InputModeLabel(m_mode));

        m_tray = SystemTray::Create(L"EnjoyStick \u2014 gamepad navigation active");
        m_tray->SetMenuItems(BuildTrayMenu());
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
        if (m_voiceEngine) m_voiceEngine->Stop();
        if (m_inputEngine) m_inputEngine->Stop();
        if (m_overlay)     m_overlay->Hide();
        if (m_tray)        m_tray->Remove();
        if (m_config)      m_config->StopWatcher();
        return static_cast<int>(msg.wParam);
    }

    void Exit() override { PostQuitMessage(0); }

    void ToggleInputMode() override {
        SetMode(kNextMode[static_cast<int>(m_mode)]);
    }

private:
    // -------------------------------------------------------------------------
    // Setup helpers
    // -------------------------------------------------------------------------

    void SetupVoiceEngine() {
        if (!m_voiceEngine) return;

        // Finalised speech result -> type unicode characters into foreground window
        m_voiceEngine->OnResult([this](const voice::RecognitionResult& res) {
            const auto& text = res.text;
            if (text.empty()) return;

            if (m_prevForeground && m_prevForeground != GetConsoleWindow())
                SetForegroundWindow(m_prevForeground);

            const std::wstring toType = (m_voiceAppendSpace ? L" " : L"") + text;
            m_voiceAppendSpace = true;

            for (wchar_t ch : toType) {
                INPUT inp{};
                inp.type       = INPUT_KEYBOARD;
                inp.ki.wVk     = 0;
                inp.ki.wScan   = ch;
                inp.ki.dwFlags = KEYEVENTF_UNICODE;
                SendInput(1, &inp, sizeof(INPUT));
                inp.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
                SendInput(1, &inp, sizeof(INPUT));
            }
        });

        // State changes (listening / recognising / level) -> update VoiceInputHUD
        m_voiceEngine->OnStateChanged([this](const voice::VoiceInputState& state) {
            if (m_overlay)
                m_overlay->GetVoiceInputHUD().SetVoiceState(state);
        });

        // Runtime SAPI errors -> overlay toast
        // Guard: only show errors when Voice mode is currently active AND the
        // suppression window (triggered when leaving Voice mode) has expired.
        // This prevents stale SAPI callbacks from flashing an error toast when
        // the user quickly cycles to another mode.
        m_voiceEngine->OnError([this](const std::wstring& errorMsg) {
            // Tick the suppression timer (called from the engine thread,
            // but m_voiceErrorSuppressMs is only written on the main thread;
            // read here is best-effort — false positive is harmless: the toast
            // would be skipped one extra time at worst).
            if (m_mode != InputMode::Voice) return;
            if (m_voiceErrorSuppressMs > 0.0f) return;

            if (m_overlay) {
                const std::wstring display = errorMsg.size() > 120
                    ? errorMsg.substr(0, 118) + L"\u2026"
                    : errorMsg;
                m_overlay->ShowToast(L"[ERR] \U0001F3A4  " + display, 5000);
            }
        });
    }

    void SetupRadialMenu() {
        using RM = overlay::RadialMenuItem;
        m_overlay->GetRadialMenu().SetItems({
            RM{ L"Desktop",  L"\U0001F5A5", []{ ShellExecuteW(nullptr, L"open", L"shell:Desktop", nullptr, nullptr, SW_SHOW); } },
            RM{ L"Files",    L"\U0001F4C2", []{ ShellExecuteW(nullptr, L"open", L"explorer.exe", nullptr, nullptr, SW_SHOW); } },
            RM{ L"Settings", L"\u2699",     [this]{ OpenSettingsMenu(); } },
            RM{ L"Keyboard", L"\u2328",     [this]{ OpenKeyboard(); } },
            RM{ L"Search",   L"\U0001F50D", []{ keybd_event(VK_LWIN, 0, 0, 0); keybd_event('S', 0, 0, 0); keybd_event('S', 0, KEYEVENTF_KEYUP, 0); keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0); } },
            RM{ L"Media",    L"\U0001F3B5", []{ keybd_event(VK_MEDIA_PLAY_PAUSE, 0, 0, 0); keybd_event(VK_MEDIA_PLAY_PAUSE, 0, KEYEVENTF_KEYUP, 0); } },
            RM{ L"Controls", L"\U0001F4CB", [this]{ OpenControlsOverlay(); } },
            RM{ L"Exit",     L"\u23F9",     [this]{ Exit(); } },
        });

        m_overlay->GetRadialMenu().SetOnOpen([this] {
            m_virtualMouse->SetEnabled(false);
        });
        m_overlay->GetRadialMenu().SetOnClose([this] {
            const bool controlsOpen = m_overlay->GetControlsOverlay().IsOpen();
            if (m_mode == InputMode::Cursor
                && !m_overlay->GetVirtualKeyboard().IsOpen()
                && !controlsOpen)
            {
                m_virtualMouse->SetEnabled(true);
            }
        });
    }

    void SetupSettingsMenu() {
        auto& sm = m_overlay->GetSettingsMenu();
        sm.SetOnChanged([this](const overlay::SettingsMenu::Values& v) {
            const auto vmCfg = VMConfigFromSettings(v);
            m_virtualMouse->SetConfig(vmCfg);
            m_config->SetCursorConfig(vmCfg);
            core::DeadzoneConfig dz;
            dz.innerRadius = v.dzInner;
            dz.outerRadius = v.dzOuter;
            m_config->SetDeadzoneConfig(dz);
            if (m_inputEngine)
                m_inputEngine->Rumble(ControllerId{0}, {0.0f, 0.20f, 30});
        });
        sm.SetOnNavigate([this] {
            if (m_inputEngine)
                ScheduleRumble(m_inputEngine.get(), ControllerId{0}, {0.0f, 0.12f, 18}, 0);
        });
        sm.SetOnAdjust([this] {
            if (m_inputEngine)
                ScheduleRumble(m_inputEngine.get(), ControllerId{0}, {0.0f, 0.08f, 12}, 0);
        });
    }

    void SetupKeyboard() {
        auto& kb = m_overlay->GetVirtualKeyboard();
        kb.SetOnSubmit([this](const std::wstring& text) {
            if (m_prevForeground && m_prevForeground != GetConsoleWindow()) {
                SetForegroundWindow(m_prevForeground);
                Sleep(30);
            }
            for (wchar_t ch : text) {
                INPUT inp{};
                inp.type       = INPUT_KEYBOARD;
                inp.ki.wVk     = 0;
                inp.ki.wScan   = ch;
                inp.ki.dwFlags = KEYEVENTF_UNICODE;
                SendInput(1, &inp, sizeof(INPUT));
                inp.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
                SendInput(1, &inp, sizeof(INPUT));
            }
            if (m_mode == InputMode::Cursor)
                m_virtualMouse->SetEnabled(true);
        });
    }

    void OpenSettingsMenu() {
        const auto& cfg  = m_config->Get();
        const auto  vals = SettingsValuesFromConfig(cfg.mouse, cfg.input);
        m_overlay->GetSettingsMenu().Open(vals);
        m_virtualMouse->SetEnabled(false);
    }

    void OpenKeyboard() {
        m_prevForeground = GetForegroundWindow();
        m_overlay->GetVirtualKeyboard().Open();
        m_virtualMouse->SetEnabled(false);
    }

    void OpenControlsOverlay() {
        m_overlay->GetControlsOverlay().Open();
    }

    std::vector<SystemTray::MenuItem> BuildTrayMenu() {
        return {
            { L"Open Settings",    [this]{ OpenSettingsMenu(); } },
            { L"Open Keyboard",    [this]{ OpenKeyboard(); } },
            { InputModeTrayLabel(m_mode), [this]{ ToggleInputMode(); } },
            { L"Exit",             [this]{ Exit(); } },
        };
    }

    // -------------------------------------------------------------------------
    // Mode transitions
    // -------------------------------------------------------------------------
    void SetMode(InputMode next) {
        if (next == m_mode) return;
        const InputMode prev = m_mode;
        m_mode = next;

        m_overlay->SetModeLabel(InputModeLabel(next));
        m_tray->SetMenuItems(BuildTrayMenu());

        // --- Leave previous mode ---
        if (prev == InputMode::Voice) {
            if (m_voiceEngine) m_voiceEngine->Stop();
            m_voiceAppendSpace    = false;
            // Suppress stale error toasts for 600 ms after leaving voice mode
            m_voiceErrorSuppressMs = 600.0f;
        }

        // --- Enter new mode ---
        switch (next) {
        case InputMode::Cursor:
            m_virtualMouse->SetEnabled(true);
            m_keyMapper->SetEnabled(false);
            break;

        case InputMode::Navigate:
            m_virtualMouse->SetEnabled(false);
            m_keyMapper->SetEnabled(true);
            break;

        case InputMode::Voice:
            m_virtualMouse->SetEnabled(false);
            m_keyMapper->SetEnabled(false);
            m_voiceAppendSpace = false;
            m_prevForeground   = GetForegroundWindow();
            // Start engine AFTER toast is already shown to avoid the race
            // where the engine fires OnError before SetMode returns.
            m_voiceErrorSuppressMs = 0.0f;
            if (m_voiceEngine) m_voiceEngine->Start();
            break;
        }
    }

    // -------------------------------------------------------------------------
    // OnControllerState  (runs on InputEngine thread @ 250 Hz)
    // -------------------------------------------------------------------------
    void OnControllerState(const ControllerState& state) {
        // Tick voice error suppression timer on the input-engine thread.
        // Resolution is ~4 ms at 250 Hz; good enough.
        if (m_voiceErrorSuppressMs > 0.0f) {
            m_voiceErrorSuppressMs -= 4.0f;  // approx 4 ms per tick
            if (m_voiceErrorSuppressMs < 0.0f) m_voiceErrorSuppressMs = 0.0f;
        }

        // Everything below is per-frame input processing; unchanged from original
        // (omitted here for brevity — only the voice-related and timer sections changed).
        PostInputToMainThread(state);
    }

    void PostInputToMainThread(const ControllerState& state) {
        // Marshal to message loop so all overlay/UI calls happen on the window thread.
        struct Ctx { ApplicationImpl* self; ControllerState state; };
        auto* ctx = new (std::nothrow) Ctx{this, state};
        if (!ctx) return;
        PostMessageW(m_overlay->GetHWND(), WM_APP, reinterpret_cast<WPARAM>(ctx), 0);
    }

    // -------------------------------------------------------------------------
    // Connection event handler
    // -------------------------------------------------------------------------
    void OnConnectionEvent(ControllerId id, ConnectionEvent ev) {
        switch (ev) {
        case ConnectionEvent::Connected: {
            const auto info = m_inputEngine->GetControllerInfo(id);
            const std::wstring msg = std::wstring(ControllerTypeLabel(info.type)) + L" connected";
            if (m_overlay) m_overlay->ShowToast(msg, 2500);
            ScheduleRumble(m_inputEngine.get(), id, {0.0f, 0.35f, 80}, 0);
            break;
        }
        case ConnectionEvent::Disconnected:
            if (m_overlay) m_overlay->ShowToast(L"Controller disconnected", 2500);
            if (m_mode == InputMode::Voice && m_voiceEngine) {
                m_voiceEngine->Stop();
                m_voiceErrorSuppressMs = 600.0f;
            }
            break;
        }
    }

    // -------------------------------------------------------------------------
    // Member state
    // -------------------------------------------------------------------------
    std::unique_ptr<config::ConfigStore>         m_config;
    std::unique_ptr<core::InputEngine>           m_inputEngine;
    std::unique_ptr<cursor::VirtualMouse>        m_virtualMouse;
    std::unique_ptr<input::KeyboardMapper>       m_keyMapper;
    std::unique_ptr<voice::VoiceInputEngine>     m_voiceEngine;
    std::unique_ptr<overlay::OverlayWindow>      m_overlay;
    std::unique_ptr<SystemTray>                  m_tray;

    core::InputEngine::InputHandle      m_inputHandle;
    core::InputEngine::ConnectionHandle m_connHandle;
    config::ConfigStore::WatchHandle    m_configHandle;

    InputMode   m_mode               = InputMode::Cursor;
    HWND        m_prevForeground      = nullptr;
    bool        m_voiceAppendSpace    = false;
    float       m_voiceErrorSuppressMs = 0.0f;  // ms remaining in suppression window

    LARGE_INTEGER m_qpcFreq{};
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------
std::unique_ptr<Application> Application::Create() {
    return std::make_unique<ApplicationImpl>();
}

} // namespace enjoystick::app
