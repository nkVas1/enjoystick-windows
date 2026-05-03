//
// Enjoystick Windows — application entry point
//
// Architecture:
//   main() runs on the UI thread and owns:
//     - OverlayWindow  (Win32 + DirectComposition rendering)
//     - ConfigStore    (JSON config + hot-reload)
//     - InputEngine    (250 Hz XInput polling on a high-priority thread)
//     - VirtualMouse   (cursor movement via SendInput)
//     - KeyboardMapper (button-to-key translation)
//     - RadialMenuController (OSK, opened by Guide button)
//     - System tray icon
//

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>

#include <enjoystick/core/InputEngine.hpp>
#include <enjoystick/cursor/VirtualMouse.hpp>
#include <enjoystick/overlay/OverlayWindow.hpp>
#include <enjoystick/overlay/RadialMenu.hpp>
#include <enjoystick/app/KeyboardMapper.hpp>
#include <enjoystick/app/AutoStart.hpp>
#include <enjoystick/config/ConfigStore.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

#pragma comment(lib, "shell32.lib")

namespace {

constexpr UINT WM_TRAYICON  = WM_USER + 1;
constexpr UINT IDM_QUIT     = 100;
constexpr UINT IDM_SETTINGS = 101;
constexpr UINT TRAY_UID     = 1;

// ---------------------------------------------------------------------------
// System tray
// ---------------------------------------------------------------------------

void AddTrayIcon(HWND hwnd, HINSTANCE hInst) {
    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = hwnd;
    nid.uID              = TRAY_UID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon            = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    if (!nid.hIcon) nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"Enjoystick Windows");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

void RemoveTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = hwnd;
    nid.uID    = TRAY_UID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void ShowTrayContextMenu(HWND hwnd) {
    POINT pt{};
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_SETTINGS, L"Settings");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_QUIT, L"Quit Enjoystick");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(hMenu);
}

// ---------------------------------------------------------------------------
// Application state (globals for WndProc access)
// ---------------------------------------------------------------------------

struct AppState {
    enjoystick::overlay::OverlayWindow*          overlay  = nullptr;
    enjoystick::overlay::RadialMenuController*   osk      = nullptr;
    enjoystick::cursor::VirtualMouse*            mouse    = nullptr;
    enjoystick::app::KeyboardMapper*             keyboard = nullptr;
    bool radialMenuOpen = false;
    bool cursorMode     = true;    // true = cursor, false = keyboard nav
};

static AppState g_app;

// ---------------------------------------------------------------------------
// WndProc (message-only window for tray and hotkeys)
// ---------------------------------------------------------------------------

LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TRAYICON:
        if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
            ShowTrayContextMenu(hwnd);
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wp) == IDM_QUIT) PostQuitMessage(0);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// WinMain
// ---------------------------------------------------------------------------

int APIENTRY wWinMain(
    _In_     HINSTANCE hInstance,
    _In_opt_ HINSTANCE,
    _In_     LPWSTR    lpCmdLine,
    _In_     int)
{
    // Single-instance guard
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"EnjoyStickWindows_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    // ---------------------------------------------------------------------------
    // Config
    // ---------------------------------------------------------------------------
    const auto configDir = std::filesystem::path(std::getenv("APPDATA")) / "EnjoyStick";
    enjoystick::config::ConfigStore configStore(configDir);
    configStore.Load();
    configStore.StartWatcher();
    const auto cfg = configStore.GetConfig();

    // ---------------------------------------------------------------------------
    // Input engine
    // ---------------------------------------------------------------------------
    auto inputEngine = enjoystick::core::InputEngine::Create(cfg.input);

    // ---------------------------------------------------------------------------
    // Virtual mouse
    // ---------------------------------------------------------------------------
    enjoystick::cursor::VirtualMouse virtualMouse(cfg.cursor);
    g_app.mouse = &virtualMouse;

    // ---------------------------------------------------------------------------
    // Keyboard mapper
    // ---------------------------------------------------------------------------
    enjoystick::app::KeyboardMapper keyboardMapper;
    g_app.keyboard = &keyboardMapper;

    // ---------------------------------------------------------------------------
    // Radial menu / OSK
    // ---------------------------------------------------------------------------
    auto osk = enjoystick::overlay::MakeDefaultOSK();
    osk.SetCloseCallback([] {
        g_app.radialMenuOpen = false;
        if (g_app.overlay) g_app.overlay->ShowRadialMenu(false);
        if (g_app.mouse)   g_app.mouse->SetEnabled(true);
    });
    g_app.osk = &osk;

    // ---------------------------------------------------------------------------
    // Overlay window
    // ---------------------------------------------------------------------------
    enjoystick::overlay::OverlayWindow::Config overlayConfig;
    overlayConfig.hInstance = hInstance;
    enjoystick::overlay::OverlayWindow overlayWindow(overlayConfig);
    if (!overlayWindow.Initialize()) return 1;
    g_app.overlay = &overlayWindow;

    // ---------------------------------------------------------------------------
    // Wire input engine → subsystems
    // ---------------------------------------------------------------------------
    using namespace std::chrono;
    auto lastTick = high_resolution_clock::now();

    auto inputHandle = inputEngine->OnInput([&](const enjoystick::ControllerState& state) {
        const auto now  = high_resolution_clock::now();
        const float dt  = duration<float>(now - lastTick).count();
        lastTick        = now;

        // Guide button opens / closes radial menu
        if (enjoystick::HasButton(state.buttonsDown, enjoystick::Button::Guide)) {
            g_app.radialMenuOpen = !g_app.radialMenuOpen;
            osk.Open();
            overlayWindow.ShowRadialMenu(g_app.radialMenuOpen);
            virtualMouse.SetEnabled(!g_app.radialMenuOpen);
        }

        if (g_app.radialMenuOpen) {
            osk.Update(state);
            return;
        }

        // Select (Back) toggles cursor / keyboard nav mode
        if (enjoystick::HasButton(state.buttonsDown, enjoystick::Button::Select)) {
            g_app.cursorMode = !g_app.cursorMode;
            virtualMouse.SetEnabled(g_app.cursorMode);
            keyboardMapper.SetEnabled(!g_app.cursorMode);
            overlayWindow.SetModeLabel(g_app.cursorMode ? L"Cursor" : L"Navigate");
        }

        if (g_app.cursorMode) {
            virtualMouse.Update(state, dt);
        } else {
            keyboardMapper.Update(state);
        }
    });

    auto connHandle = inputEngine->OnConnection([&](enjoystick::ControllerId id,
                                                     enjoystick::ConnectionEvent ev) {
        overlayWindow.SetControllerConnected(id, ev == enjoystick::ConnectionEvent::Connected);
        if (cfg.ui.toastsEnabled) {
            overlayWindow.ShowToast(
                ev == enjoystick::ConnectionEvent::Connected
                ? L"\U0001F3AE Controller connected"
                : L"Controller disconnected");
        }
        overlayWindow.ShowHUD(true);
    });

    inputEngine->Start();

    // ---------------------------------------------------------------------------
    // Autostart
    // ---------------------------------------------------------------------------
    if (cfg.autostart.enabled) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        enjoystick::app::AutoStart::Enable(exePath);
    }

    // ---------------------------------------------------------------------------
    // Tray message-only window
    // ---------------------------------------------------------------------------
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = TrayWndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"EnjoyStickTray";
    RegisterClassExW(&wc);
    HWND hTray = CreateWindowExW(0, L"EnjoyStickTray", nullptr, 0,
                                  0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);
    AddTrayIcon(hTray, hInstance);

    // ---------------------------------------------------------------------------
    // Main loop
    // ---------------------------------------------------------------------------
    constexpr float kTargetFPS  = 60.0f;
    constexpr int   kFrameMs    = static_cast<int>(1000.0f / kTargetFPS);

    MSG msg{};
    while (true) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto Shutdown;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!overlayWindow.PumpMessages()) break;
        overlayWindow.Render();
        Sleep(kFrameMs);
    }

Shutdown:
    RemoveTrayIcon(hTray);
    inputEngine->Stop();
    configStore.StopWatcher();
    CloseHandle(hMutex);
    return static_cast<int>(msg.wParam);
}
