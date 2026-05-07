#pragma once

// ---------------------------------------------------------------------------
// VoiceInputEngine
//
// Application-layer facade over VoiceInput.
// Provides a clean, callback-based interface that Application.cpp uses:
//
//   auto engine = VoiceInputEngine::Create({});
//   engine->OnResult([](const RecognitionResult& r){ ... });
//   engine->OnStateChanged([](const VoiceInputState& s){ ... });
//   engine->Start();
//   engine->Stop();
//
// Internally owns a VoiceInput instance and re-exposes its callbacks.
// ---------------------------------------------------------------------------

#include <enjoystick/voice/VoiceInput.hpp>

#include <functional>
#include <memory>
#include <string>

namespace enjoystick::voice {

/// Result of a completed recognition phrase.
struct RecognitionResult {
    std::wstring text;
    VoiceLanguage language = VoiceLanguage::Russian;
};

class VoiceInputEngine {
public:
    struct Config {
        VoiceLanguage defaultLanguage = VoiceLanguage::Russian;
    };

    static std::unique_ptr<VoiceInputEngine> Create(Config config = {});

    virtual ~VoiceInputEngine() = default;

    /// Register callback fired on each finalised recognition result.
    virtual void OnResult(std::function<void(const RecognitionResult&)> cb) = 0;

    /// Register callback fired whenever the voice engine state changes
    /// (listening/recognising flag, audio level, partial text).
    virtual void OnStateChanged(std::function<void(const VoiceInputState&)> cb) = 0;

    /// Arm the microphone and begin recognition.
    virtual void Start() = 0;

    /// Disarm and stop recognition.
    virtual void Stop() = 0;

    /// Cycle the active recognition language (RU ↔ EN).
    virtual void CycleLanguage() = 0;

    [[nodiscard]] virtual bool          IsActive()   const noexcept = 0;
    [[nodiscard]] virtual VoiceLanguage GetLanguage() const noexcept = 0;
};

} // namespace enjoystick::voice
