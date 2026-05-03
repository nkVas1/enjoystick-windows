#include <enjoystick/app/Application.hpp>
#include "AutoStart.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <stdexcept>
#include <format>

namespace enjoystick::app {

// ---------------------------------------------------------------------------
// InputMode
// ---------------------------------------------------------------------------

enum class InputMode : uint8_t {
    Cursor,    ///< Right-stick = mouse, triggers = clicks
    Navigate,  ///< D-pad / buttons = keyboard nav
};

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

class ApplicationImpl final : public Application {
public:
    ApplicationImpl() = default;
    ~ApplicationImpl() override { Exit(); }

    void Init() override {
        // ─── Config
        m_config = config::ConfigStore::Create();
        m_config->Load();
        m_config->StartWatcher();

        // ─── Input engine
        core::InputEngine::Config engCfg;
        engCfg.pollingRateHz    = 250;
        engCfg.hapticsEnabled   = true;
        m_inputEngine = core::InputEngine::Create(engCfg);

        // ─── Subsystems wired to config
        const auto& cfg = m_config->Get();
        m_virtualMouse  = std::make_unique<cursor::VirtualMouse>(cfg.cursor);
        m_keyMapper     = std::make_unique<input::KeyboardMapper>();

        // ─── Overlay
        m_overlay = overlay::OverlayWindow::Create();
        SetupRadialMenu();
        m_overlay->Show();

        // ─── Wire input callbacks
        m_inputHandle = m_inputEngine->OnInput([this](const ControllerState& s) {
            OnControllerState(s);
        });
        m_connHandle = m_inputEngine->OnConnection([this](ControllerId id, ConnectionEvent ev) {
            OnConnectionEvent(id, ev);
        });

        // ─── React to config changes
        m_configHandle = m_config->OnChanged([this](const AppConfig& c) {
            m_virtualMouse->SetConfig(c.cursor);
        });

        m_inputEngine->Start();

        // ─── AutoStart (silently enable on first run)
        if (!AutoStart::IsEnabled()) AutoStart::Enable();
    }

    int Run() override {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return static_cast<int>(msg.wParam);
    }

    void Exit() override {
        if (m_inputEngine) m_inputEngine->Stop();
        if (m_overlay)     m_overlay->Hide();
        if (m_config)      m_config->StopWatcher();
        PostQuitMessage(0);
    }

    void ToggleInputMode() override {
        if (m_mode == InputMode::Cursor) {
            m_mode = InputMode::Navigate;
            m_virtualMouse->SetEnabled(false);
            m_keyMapper->SetEnabled(true);
        } else {
            m_mode = InputMode::Cursor;
            m_virtualMouse->SetEnabled(true);
            m_keyMapper->SetEnabled(false);
        }
    }

private:
    void OnControllerState(const ControllerState& state) {
        // Dispatch delta time from last call
        const uint64_t now = []() -> uint64_t {
            LARGE_INTEGER li; QueryPerformanceCounter(&li); return li.QuadPart;
        }();
        const float dt = (m_lastTick == 0) ? 0.004f
            : static_cast<float>(now - m_lastTick) / static_cast<float>([]() -> uint64_t {
                LARGE_INTEGER li; QueryPerformanceFrequency(&li); return li.QuadPart;
            }());
        m_lastTick = now;

        // Guide button → toggle radial menu
        if (HasButton(state.buttonsDown, Button::Guide)) {
            if (m_overlay->GetRadialMenu().IsVisible())
                m_overlay->GetRadialMenu().Close();
            else
                m_overlay->GetRadialMenu().Open();
        }

        m_virtualMouse->Update(state, dt);
        m_keyMapper->Update(state);
        m_overlay->PostState(state);
    }

    void OnConnectionEvent(ControllerId id, ConnectionEvent ev) {
        const bool connected = ev == ConnectionEvent::Connected;
        m_overlay->ShowToast(
            connected ? L"🎮 Controller connected"
                      : L"⚠ Controller disconnected");
        if (connected) {
            // Gentle welcome rumble
            m_inputEngine->Rumble(id, {0.3f, 0.6f, 150});
        }
    }

    void SetupRadialMenu() {
        m_overlay->GetRadialMenu().SetItems({
            { L"🖥️",  L"Desktop",    [this]{ ShellExecuteW(nullptr,L"open",L"shell:Desktop",nullptr,nullptr,SW_SHOW); } },
            { L"🎵",   L"Media",      [this]{ ShellExecuteW(nullptr,L"open",L"shell:AppFolder",nullptr,nullptr,SW_SHOW); } },
            { L"⚙️",   L"Settings",   [this]{ ShellExecuteW(nullptr,L"open",L"ms-settings:",nullptr,nullptr,SW_SHOW); } },
            { L"🔍",   L"Search",     [this]{ keybd_event(VK_LWIN, 0, 0, 0); keybd_event('S', 0, 0, 0); keybd_event('S', 0, KEYEVENTF_KEYUP, 0); keybd_event(VK_LWIN, 0, KEYEVENTF_KEYUP, 0); } },
            { L"📂",   L"Files",      [this]{ ShellExecuteW(nullptr,L"open",L"explorer.exe",nullptr,nullptr,SW_SHOW); } },
            { L"🔁",   L"Mode",       [this]{ ToggleInputMode(); } },
            { L"⏹️",   L"Exit",       [this]{ Exit(); } },
        });
    }

    // Subsystems
    std::unique_ptr<config::ConfigStore>       m_config;
    std::unique_ptr<core::InputEngine>          m_inputEngine;
    std::unique_ptr<cursor::VirtualMouse>       m_virtualMouse;
    std::unique_ptr<input::KeyboardMapper>      m_keyMapper;
    std::unique_ptr<overlay::OverlayWindow>     m_overlay;

    // Callback lifetime handles
    CallbackHandle m_inputHandle;
    CallbackHandle m_connHandle;
    CallbackHandle m_configHandle;

    InputMode m_mode     = InputMode::Cursor;
    uint64_t  m_lastTick = 0;
};

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------

std::unique_ptr<Application> Application::Create() {
    return std::make_unique<ApplicationImpl>();
}

} // namespace enjoystick::app
