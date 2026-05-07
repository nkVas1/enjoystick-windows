#pragma once

// WIN32_LEAN_AND_MEAN and NOMINMAX are injected by CMake target_compile_definitions.
#include <Windows.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <enjoystick/overlay/OverlayWindow.hpp>
#include <enjoystick/overlay/VirtualKeyboard.hpp>
#include <enjoystick/overlay/ControlsOverlay.hpp>
#include <enjoystick/overlay/VoiceInputHUD.hpp>

#include "Overlay_SpringAnim.hpp"

#include <atomic>
#include <mutex>
#include <thread>
#include <queue>
#include <string>
#include <vector>

namespace enjoystick::overlay {

// Toast category decoded from message prefix tag.
// Prefix tags: [OK] = Success, [WARN] = Warning, [ERR] = Error, default = Info
enum class ToastCategory : uint8_t { Info, Success, Warning, Error };

struct ToastNotification {
    std::wstring   message;
    uint32_t       durationMs   = 2500;
    float          elapsed      = 0.0f;
    float          slideX       = 1.0f;   // 0 = fully visible (target), 1 = off-screen right
    float          slideV       = 0.0f;   // spring velocity
    ToastCategory  category     = ToastCategory::Info;

    // Critically-damped spring step (inlined for simplicity)
    void StepSlide(float dt) noexcept {
        if (dt <= 0.0f) return;
        if (dt > 0.05f) dt = 0.05f;
        const float stiffness = 340.0f, damping = 22.0f;
        const float spring = (0.0f - slideX) * stiffness;
        const float damp   = slideV * (-damping);
        slideV = slideV + (spring + damp) * dt;
        slideX = slideX + slideV * dt;
    }
};

///
/// OverlayWindowImpl — layered click-through window with per-pixel alpha.
///
class OverlayWindowImpl final : public OverlayWindow {
public:
    explicit OverlayWindowImpl(Config config);
    ~OverlayWindowImpl() override;

    void Show()  override;
    void Hide()  override;
    void PostState(const ControllerState& state) override;
    RadialMenu&      GetRadialMenu()      override;
    SettingsMenu&    GetSettingsMenu()    override;
    VirtualKeyboard& GetVirtualKeyboard() override;
    ControlsOverlay& GetControlsOverlay() override;
    VoiceInputHUD&   GetVoiceInputHUD()   override;
    void ShowToast(std::wstring message, uint32_t durationMs) override;
    void SetModeLabel(std::wstring label)       override;
    [[nodiscard]] bool    IsShown()  const noexcept override;
    [[nodiscard]] HWND__* GetHWND()  const noexcept override;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void RenderThread();
    void CreateWindowAndD2D();
    void DestroyWindowAndD2D();
    void EnsureDib(int w, int h);
    void DestroyDib();
    bool EnsureRenderTarget();
    void PumpMessages();
    void RenderFrame(float deltaSeconds);

    void DrawActiveIndicator(ID2D1RenderTarget* rt);
    void DrawHudMode(ID2D1RenderTarget* rt, float deltaSeconds);
    void DrawToasts(ID2D1RenderTarget* rt, float deltaSeconds);
    void DrawStickViz(ID2D1RenderTarget* rt, float pillRight, float pillCy,
                      Vec2 stickSource) const;

    static ToastCategory DecodeCategory(const std::wstring& msg) noexcept;

    Config   m_config;
    HWND     m_hwnd    = nullptr;
    HMONITOR m_monitor = nullptr;
    Rect     m_monitorRect = {};

    Microsoft::WRL::ComPtr<ID2D1Factory1>       m_d2dFactory;
    Microsoft::WRL::ComPtr<IDWriteFactory>      m_dwriteFactory;
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> m_renderTarget;

    HDC     m_memDC  = nullptr;
    HBITMAP m_hDib   = nullptr;
    int     m_dibW   = 0;
    int     m_dibH   = 0;
    bool    m_dibDirty = true;

#pragma warning(push)
#pragma warning(disable: 4324)
    alignas(64) std::atomic<ControllerState*> m_pendingState{nullptr};
#pragma warning(pop)
    ControllerState m_stateBuffers[2];
    int             m_activeBuffer = 0;
    ControllerState m_lastState    = {};

    mutable std::mutex m_modeLabelMutex;
    std::wstring       m_modeLabel;

    RadialMenu      m_radialMenu;
    SettingsMenu    m_settingsMenu;
    VirtualKeyboard m_keyboard;
    ControlsOverlay m_controlsOverlay;
    VoiceInputHUD   m_voiceHUD;          // <<< new

    std::mutex                      m_toastMutex;
    std::queue<ToastNotification>   m_pendingToasts;
    std::vector<ToastNotification>  m_activeToasts;

    // Pending VoiceInputState snapshot posted from the app layer (thread-safe)
    mutable std::mutex              m_voiceStateMutex;
    voice::VoiceInputState          m_voiceState;

    FloatSpring m_hudPillWidthSpring;
    float       m_hudPillPhase = 0.0f;

    static constexpr float kHudCrossDuration = 0.160f;
    std::wstring m_hudPrevLabel;
    float        m_hudCrossT   = 1.0f;
    bool         m_hudCrossDir = true;

    FloatSpring  m_stickVizSpring;
    bool         m_stickVizUseRight = false;

    std::thread        m_renderThread;
    std::atomic<bool>  m_running{false};
    std::atomic<bool>  m_shown{false};
    HANDLE             m_startEvent = nullptr;

    float m_dpiScale = 1.0f;
};

} // namespace enjoystick::overlay
