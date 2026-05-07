#pragma once

// ---------------------------------------------------------------------------
// VoiceInput
//
// Wraps Windows Speech API (SAPI 5) for continuous dictation.
// Supports Russian (ru-RU) as primary language and English (en-US) as
// fallback / secondary, controlled by SetLanguage().
//
// Threading:
//   Start() / Stop()  -- called from the main thread
//   The recognition callback fires on a dedicated SAPI worker thread;
//   the result is marshalled to the main thread via a Win32 posted message
//   or a lock-free ring so callers never need to worry about reentrancy.
//
// Usage:
//   auto vi = VoiceInput::Create();
//   vi->SetOnResult([](const std::wstring& text){ /* inject text */ });
//   vi->Start();   // arms the mic
//   vi->Stop();    // disarms
// ---------------------------------------------------------------------------

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <functional>
#include <memory>
#include <string>

namespace enjoystick::voice {

enum class VoiceLanguage : uint8_t {
    Russian,   // ru-RU  (primary, preferred)
    English,   // en-US
};

struct VoiceInputState {
    bool    listening   = false;  ///< Mic is armed and waiting for speech
    bool    recognizing = false;  ///< Speech is being actively processed
    float   level       = 0.0f;  ///< Normalised audio level [0..1] for VU meter
    std::wstring partial;        ///< In-progress hypothesis (may be empty)
};

class VoiceInput {
public:
    using OnResultCallback  = std::function<void(const std::wstring& text)>;
    using OnStateCallback   = std::function<void(const VoiceInputState&)>;
    using OnErrorCallback   = std::function<void(const std::wstring& error)>;

    static std::unique_ptr<VoiceInput> Create();

    virtual ~VoiceInput() = default;

    // ---- Configuration (must be called before Start) -----------------------
    virtual void SetLanguage(VoiceLanguage lang) = 0;
    virtual void SetOnResult(OnResultCallback cb) = 0;
    virtual void SetOnState (OnStateCallback  cb) = 0;
    virtual void SetOnError (OnErrorCallback  cb) = 0;

    // ---- Lifetime ----------------------------------------------------------
    /// Initialise SAPI, load the recognition profile and arm the microphone.
    /// Returns false if SAPI is unavailable or the language is not installed.
    virtual bool Start()  = 0;
    virtual void Stop()   = 0;

    // ---- Language hot-swap -------------------------------------------------
    /// Switch recognition language on the fly (restarts the engine internally).
    virtual void CycleLanguage() = 0;

    // ---- Inspection --------------------------------------------------------
    [[nodiscard]] virtual bool              IsListening()  const noexcept = 0;
    [[nodiscard]] virtual VoiceLanguage     GetLanguage()  const noexcept = 0;
    [[nodiscard]] virtual VoiceInputState   GetState()     const noexcept = 0;
};

} // namespace enjoystick::voice
