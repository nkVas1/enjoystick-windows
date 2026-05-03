#pragma once

#include <enjoystick/core/InputEngine.hpp>
#include <enjoystick/cursor/VirtualMouse.hpp>
#include <enjoystick/input/KeyboardMapper.hpp>
#include <enjoystick/overlay/OverlayWindow.hpp>
#include <enjoystick/config/ConfigStore.hpp>
#include <memory>

namespace enjoystick::app {

///
/// Application — top-level composition root.
///
/// Owns and wires together every subsystem:
///   InputEngine → VirtualMouse + KeyboardMapper + OverlayWindow
///
/// Lifecycle:
///   auto app = Application::Create();
///   app->Init();
///   app->Run();   // blocks until Exit() is called
///
/// The single message pump on the calling thread handles Win32 messages;
/// all other work happens on subsystem threads.
///
class Application {
public:
    static std::unique_ptr<Application> Create();

    virtual ~Application() = default;

    /// Initialise all subsystems.
    virtual void Init() = 0;

    /// Enter the Win32 message loop (blocks until Exit()).
    virtual int  Run()  = 0;

    /// Post WM_QUIT to exit the message loop cleanly.
    virtual void Exit() = 0;

    /// Toggle between Cursor mode and Gamepad-nav mode.
    virtual void ToggleInputMode() = 0;
};

} // namespace enjoystick::app
