#include <enjoystick/app/Application.hpp>

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <objbase.h>  // CoInitializeEx / CoUninitialize
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
    (void)lpCmdLine;

    // Ensure only one instance runs at a time.
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"EnjoyStick_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) ReleaseMutex(hMutex);
        return 0;
    }

    // Initialise COM (apartment-threaded) for SAPI, shell operations, etc.
    // The dispatch HWND for VoiceInput lives on this thread and receives
    // WM_VOICE_EVENT messages pumped by Application::Run()'s GetMessage loop.
    const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool comInited = SUCCEEDED(hrCom);  // S_FALSE = already initialised by another library

    int exitCode = 0;
    try {
        auto app = enjoystick::app::Application::Create();
        app->Init();
        exitCode = app->Run();
    } catch (const std::exception& ex) {
        MessageBoxA(nullptr, ex.what(), "EnjoyStick \xe2\x80\x94 Fatal Error",
                    MB_ICONERROR | MB_OK);
        exitCode = 1;
    }

    if (comInited) CoUninitialize();
    if (hMutex) ReleaseMutex(hMutex);
    return exitCode;
}
