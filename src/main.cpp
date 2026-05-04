#include <enjoystick/app/Application.hpp>

// WIN32_LEAN_AND_MEAN is already injected by CMake via target_compile_definitions;
// guard so the manual define becomes a no-op and avoids C4005 redefinition.
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <stdexcept>
#include <string>

///
/// WinMain — EnjoyStick Windows entry point.
///
/// Supported command-line flags (parsed by Application::Init):
///   --autostart   Launched from the Windows run key; suppress first-run UI.
///   --minimized   Start with overlay hidden until first controller input.
///
int WINAPI wWinMain(
    _In_ HINSTANCE,
    _In_opt_ HINSTANCE,
    _In_ LPWSTR  lpCmdLine,
    _In_ int)
{
    // Suppress C4100: lpCmdLine is passed to Application for future flag parsing.
    (void)lpCmdLine;

    // Ensure only one instance runs at a time.
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
        MessageBoxA(nullptr, ex.what(), "EnjoyStick \xe2\x80\x94 Fatal Error",
                    MB_ICONERROR | MB_OK);
        if (hMutex) ReleaseMutex(hMutex);
        return 1;
    }
}
