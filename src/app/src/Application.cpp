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

        m_voiceEngine->OnStateChanged([this](const voice::VoiceInputState& state) {
            if (m_overlay) {
                m_overlay->GetVoiceInputHUD().SetVoiceState(state);
            }
        });

        // Show a toast if SAPI fails to initialise so the user knows why the
        // mic icon stays inactive instead of silently doing nothing.
        m_voiceEngine->OnError([this](const voice::RecognitionResult& /*unused*/) {
            // OnError on the engine is not yet bridged; handled at voice layer
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
        m_overlay->GetRadialMenu().Close();
    }

    void OpenKeyboard() {
        m_prevForeground = GetForegroundWindow();
        m_virtualMouse->SetEnabled(false);
        m_overlay->GetVirtualKeyboard().Open();
        m_overlay->GetRadialMenu().Close();
        m_overlay->ShowToast(L"\u2328  Virtual keyboard \u2014 [\u25B2] submit  [\u25C6] cancel", 3000);
    }

    void OpenControlsOverlay() {
        m_virtualMouse->SetEnabled(false);
        m_overlay->GetControlsOverlay().Open();
        m_overlay->GetRadialMenu().Close();
        m_overlay->ShowToast(L"\U0001F4CB  Controls reference \u2014 [\u25C4/\u25BA] sections  [\u25C6] close", 3000);
    }

    void EnterVoiceMode() {
        m_prevForeground   = GetForegroundWindow();
        m_voiceAppendSpace = false;
        m_virtualMouse->SetEnabled(false);
        m_keyMapper->SetEnabled(false);

        auto& hud = m_overlay->GetVoiceInputHUD();
        hud.SetLanguageLabel(L"RU");
        hud.Open();

        if (m_voiceEngine) {
            const bool ok = m_voiceEngine->Start();
            if (!ok) {
                m_overlay->ShowToast(
                    L"[ERR] \U0001F3A4  Voice unavailable \u2014 check SAPI / microphone", 5000);
                // Don't exit the mode — HUD still shows so the user can cancel
            } else {
                SyncVoiceHudLang();
            }
        }

        m_overlay->SetModeLabel(InputModeLabel(InputMode::Voice));
        m_overlay->ShowToast(
            L"\U0001F3A4  Voice mode \u2014 LB+RB exit  \u2665 lang", 3500);
        if (m_inputEngine)
            m_inputEngine->Rumble(ControllerId{0}, {0.0f, 0.35f, 60});
    }

    void ExitVoiceMode() {
        if (m_voiceEngine) m_voiceEngine->Stop();
        m_overlay->GetVoiceInputHUD().Close();
        m_voiceAppendSpace = false;
    }

    void SyncVoiceHudLang() {
        if (!m_voiceEngine) return;
        const wchar_t* label =
            (m_voiceEngine->GetLanguage() == voice::VoiceLanguage::Russian)
            ? L"RU" : L"EN";
        m_overlay->GetVoiceInputHUD().SetLanguageLabel(label);
    }

    void SetMode(InputMode newMode) {
        if (m_mode == newMode) return;
        const InputMode prevMode = m_mode;
        m_mode = newMode;

        if (prevMode == InputMode::Voice) ExitVoiceMode();

        const bool cursor    = (m_mode == InputMode::Cursor);
        const bool navigate  = (m_mode == InputMode::Navigate);
        const bool voiceMode = (m_mode == InputMode::Voice);

        const bool menuOpen = m_overlay->GetRadialMenu().IsVisible();
        const bool kbOpen   = m_overlay->GetVirtualKeyboard().IsOpen();
        const bool ctrlOpen = m_overlay->GetControlsOverlay().IsOpen();

        m_virtualMouse->SetEnabled(cursor && !menuOpen && !kbOpen && !ctrlOpen);
        m_keyMapper->SetEnabled(navigate);

        if (voiceMode) {
            EnterVoiceMode();
        } else {
            if (m_overlay) {
                m_overlay->SetModeLabel(InputModeLabel(m_mode));
                m_overlay->ShowToast(InputModeLabel(m_mode));
            }
            if (m_inputEngine) {
                m_inputEngine->Rumble(ControllerId{0}, {0.0f, 0.55f, 80});
                ScheduleRumble(m_inputEngine.get(), ControllerId{0}, {0.0f, 0.40f, 60}, 120);
            }
        }

        if (m_tray) m_tray->SetMenuItems(BuildTrayMenu());
    }

    // -------------------------------------------------------------------------
    // Controller state handler
    // -------------------------------------------------------------------------

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
        const bool kbOpen       = m_overlay->GetVirtualKeyboard().IsOpen();
        const bool controlsOpen = m_overlay->GetControlsOverlay().IsOpen();
        const bool voiceOpen    = m_overlay->GetVoiceInputHUD().IsOpen();

        // ------------------------------------------------------------------
        // Voice mode controls
        // ------------------------------------------------------------------
        if (voiceOpen) {
            // LB+RB — exit voice mode
            if (held(Button::LB) && held(Button::RB)) {
                if (!m_lbRbChordActive) {
                    m_lbRbChordActive = true;
                    ToggleInputMode();
                }
            } else {
                m_lbRbChordActive = false;
            }

            // West (B on Xbox) — cycle recognition language RU↔EN
            if (pressed(Button::West)) {
                if (m_voiceEngine) {
                    m_voiceEngine->CycleLanguage();
                    SyncVoiceHudLang();
                    const wchar_t* langName =
                        (m_voiceEngine->GetLanguage() == voice::VoiceLanguage::Russian)
                        ? L"Russian (RU)" : L"English (EN)";
                    m_overlay->ShowToast(
                        std::wstring(L"\U0001F3A4  Language: ") + langName, 2000);
                }
            }

            // Select (View) — reset append-space for new sentence start
            if (pressed(Button::Select)) {
                m_voiceAppendSpace = false;
                m_overlay->ShowToast(L"\U0001F3A4  New sentence", 1200);
            }

            m_overlay->PostState(state);
            return;
        }

        if (controlsOpen) {
            m_overlay->GetControlsOverlay().Update(state, dt * 0.001f);
            if (!m_overlay->GetControlsOverlay().IsOpen()) {
                if (m_mode == InputMode::Cursor)
                    m_virtualMouse->SetEnabled(true);
            }
            m_prevButtons = state.buttons;
            return;
        }

        if (kbOpen) {
            m_overlay->GetVirtualKeyboard().Update(state, dt * 0.001f);
            if (!m_overlay->GetVirtualKeyboard().IsOpen() && m_mode == InputMode::Cursor)
                m_virtualMouse->SetEnabled(true);
            m_prevButtons = state.buttons;
            return;
        }

        if (pressed(Button::Guide)) { m_guideChordUsed = false; }

        if (held(Button::Guide)) {
            if (pressed(Button::Start)) {
                m_guideChordUsed = true;
                if (settingsOpen) m_overlay->GetSettingsMenu().Close();
                else              OpenSettingsMenu();
                return;
            }
            if (pressed(Button::LB)) {
                m_guideChordUsed = true;
                if (!kbOpen) OpenKeyboard();
                return;
            }
        }

        if (released(Button::Guide) && !m_guideChordUsed) {
            if (settingsOpen) {
                m_overlay->GetSettingsMenu().Close();
            } else {
                auto& rm = m_overlay->GetRadialMenu();
                if (rm.IsVisible()) rm.Close();
                else                rm.Open();
            }
            return;
        }

        if (pressed(Button::Start) && !held(Button::Guide)) {
            OpenKeyboard();
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

        // LB+RB — cycle modes
        if (held(Button::LB) && held(Button::RB)) {
            if (!m_lbRbChordActive) { m_lbRbChordActive = true; ToggleInputMode(); }
        } else {
            m_lbRbChordActive = false;
        }

        if (m_mode == InputMode::Cursor) {
            if (!held(Button::LB) || !held(Button::RB)) {
                if (pressed(Button::LB)) { SendBrowserTab(false); }
                if (pressed(Button::RB)) { SendBrowserTab(true);  }
            }

            if (pressed(Button::LS))
                SendDoubleClick(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);

            if (pressed(Button::North)) m_virtualMouse->MiddleClick();

            if (pressed(Button::West)) {
                SendXButton(XBUTTON1, true);
                SendXButton(XBUTTON1, false);
            }

            const bool selectHeld = held(Button::Select);
            const int scrollMult = selectHeld ? 5 : 1;

            if (pressed(Button::DPadLeft) && !selectHeld) {
                SendKey(VK_MENU, true, true); SendKey(VK_LEFT, true, true);
                SendKey(VK_LEFT, false, true); SendKey(VK_MENU, false, true);
            }
            if (pressed(Button::DPadRight) && !selectHeld) {
                SendKey(VK_MENU, true, true); SendKey(VK_RIGHT, true, true);
                SendKey(VK_RIGHT, false, true); SendKey(VK_MENU, false, true);
            }
            if (pressed(Button::DPadUp))   SendScrollLines(+scrollMult);
            if (pressed(Button::DPadDown)) SendScrollLines(-scrollMult);

            if (!selectHeld && pressed(Button::Select)) {
                SendKey(VK_LWIN, true);
                SendKey(VK_TAB, true, true); SendKey(VK_TAB, false, true);
                SendKey(VK_LWIN, false);
            }

            if (pressed(Button::RS)) { OpenKeyboard(); return; }

            if (held(Button::South)) {
                m_southHoldMs += dt;
                if (m_southHoldMs >= kSouthLongPressMs && !m_southDragActive) {
                    m_southDragActive = true;
                    m_virtualMouse->LeftDown();
                }
            }
            if (released(Button::South)) {
                if (m_southDragActive)       m_virtualMouse->LeftUp();
                else if (m_southHoldMs > 0.0f && m_southHoldMs < kSouthLongPressMs)
                    m_virtualMouse->LeftClick();
                m_southHoldMs     = 0.0f;
                m_southDragActive = false;
            }

            if (pressed(Button::East)) m_virtualMouse->RightClick();
        }

        m_virtualMouse->Update(state, dt);
        m_keyMapper->Update(state);
        m_overlay->PostState(state);
    }

    void OnConnectionEvent(ControllerId id, ConnectionEvent ev) {
        const bool connected = (ev == ConnectionEvent::Connected);
        std::wstring typeLabel;
        if (m_inputEngine) {
            const auto list = m_inputEngine->GetConnectedControllers();
            for (const auto& info : list)
                if (info.id == id) { typeLabel = ControllerTypeLabel(info.type); break; }
        }
        if (typeLabel.empty()) typeLabel = L"Controller";
        const std::wstring msg = connected
            ? (L"[OK] \U0001F3AE  " + typeLabel + L" connected")
            : (L"[WARN] \u26A0  "   + typeLabel + L" disconnected");
        if (m_overlay) m_overlay->ShowToast(msg);
        if (m_tray)    m_tray->ShowBalloon(L"EnjoyStick", msg);
        if (connected && m_inputEngine)
            m_inputEngine->Rumble(id, {0.3f, 0.6f, 150});
    }

    std::vector<TrayMenuItem> BuildTrayMenu() {
        const bool autoOn = AutoStart::IsEnabled();
        const wchar_t* autoLabel = autoOn ? L"\u2713 Launch on login" : L"  Launch on login";
        return {
            { L"EnjoyStick v0.1", {},       false },
            { L"",                {},       true  },
            { InputModeTrayLabel(m_mode), [this]{ ToggleInputMode(); } },
            { L"Open Settings",     [this]{ OpenSettingsMenu(); } },
            { L"Open Keyboard",     [this]{ OpenKeyboard(); } },
            { L"Controls Reference",[this]{ OpenControlsOverlay(); } },
            { autoLabel, [autoOn]{ if (autoOn) AutoStart::Disable(); else AutoStart::Enable(); } },
            { L"",     {},  true },
            { L"Exit", [this]{ Exit(); } },
        };
    }

    // -------------------------------------------------------------------------
    // Members
    // -------------------------------------------------------------------------

    std::unique_ptr<config::ConfigStore>       m_config;
    std::unique_ptr<core::InputEngine>         m_inputEngine;
    std::unique_ptr<cursor::VirtualMouse>      m_virtualMouse;
    std::unique_ptr<input::KeyboardMapper>     m_keyMapper;
    std::unique_ptr<voice::VoiceInputEngine>   m_voiceEngine;
    std::unique_ptr<overlay::OverlayWindow>    m_overlay;
    std::unique_ptr<SystemTray>                m_tray;

    CallbackHandle               m_inputHandle;
    CallbackHandle               m_connHandle;
    config::ConfigCallbackHandle m_configHandle;

    InputMode      m_mode            = InputMode::Cursor;
    int64_t        m_lastTick        = 0;
    LARGE_INTEGER  m_qpcFreq         = {};
    Button         m_prevButtons     = Button::None;
    bool           m_lbRbChordActive = false;
    bool           m_guideChordUsed  = false;
    bool           m_voiceAppendSpace = false;

    HWND           m_prevForeground   = nullptr;

    static constexpr float kSouthLongPressMs = 600.0f;
    float m_southHoldMs     = 0.0f;
    bool  m_southDragActive = false;
};

std::unique_ptr<Application> Application::Create() {
    return std::make_unique<ApplicationImpl>();
}

} // namespace enjoystick::app
