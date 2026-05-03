#include <enjoystick/app/Application.hpp>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdexcept>
#include <string>

///
/// WinMain — Enjoystick Windows entry point.
///
/// Flags:
///   --autostart   Launched by the Windows run key; suppress first-run dialogs.
///
int WINAPI wWinMain(
    _In_ HINSTANCE,
    _In_opt_ HINSTANCE,
    _In_ LPWSTR lpCmdLine,
    _In_ int)
{
    // Ensure only one instance runs at a time
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"EnjoyStick_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) ReleaseMutex(hMutex);
        return 0;
    }

    try {
        auto app = enjoystick::app::Application::Create();
        app->Init();
        const int result = app->Run();
        if (hMutex) ReleaseMutex(hMutex);
        return result;
    } catch (const std::exception& ex) {
        MessageBoxA(nullptr, ex.what(), "EnjoyStick — Fatal Error",
                    MB_ICONERROR | MB_OK);
        if (hMutex) ReleaseMutex(hMutex);
        return 1;
    }
}
