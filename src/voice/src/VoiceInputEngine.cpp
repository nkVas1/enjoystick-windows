#include <enjoystick/voice/VoiceInputEngine.hpp>

#include <utility>

namespace enjoystick::voice {

class VoiceInputEngineImpl final : public VoiceInputEngine {
public:
    explicit VoiceInputEngineImpl(Config config)
        : m_config(config)
    {
        m_input = VoiceInput::Create();
        m_input->SetLanguage(config.defaultLanguage);

        // Bridge VoiceInput::SetOnResult  →  RecognitionResult callback
        m_input->SetOnResult([this](const std::wstring& text) {
            if (m_onResult) {
                RecognitionResult r;
                r.text     = text;
                r.language = m_input->GetLanguage();
                m_onResult(r);
            }
        });

        // Bridge VoiceInput::SetOnState  →  OnStateChanged callback
        m_input->SetOnState([this](const VoiceInputState& state) {
            if (m_onStateChanged) m_onStateChanged(state);
        });
    }

    void OnResult(std::function<void(const RecognitionResult&)> cb) override {
        m_onResult = std::move(cb);
    }

    void OnStateChanged(std::function<void(const VoiceInputState&)> cb) override {
        m_onStateChanged = std::move(cb);
    }

    void Start() override {
        if (!m_active) {
            m_active = m_input->Start();
        }
    }

    void Stop() override {
        if (m_active) {
            m_input->Stop();
            m_active = false;
        }
    }

    void CycleLanguage() override {
        m_input->CycleLanguage();
    }

    [[nodiscard]] bool          IsActive()   const noexcept override { return m_active; }
    [[nodiscard]] VoiceLanguage GetLanguage() const noexcept override { return m_input->GetLanguage(); }

private:
    Config                                         m_config;
    std::unique_ptr<VoiceInput>                    m_input;
    std::function<void(const RecognitionResult&)>  m_onResult;
    std::function<void(const VoiceInputState&)>    m_onStateChanged;
    bool                                           m_active = false;
};

std::unique_ptr<VoiceInputEngine> VoiceInputEngine::Create(Config config) {
    return std::make_unique<VoiceInputEngineImpl>(std::move(config));
}

} // namespace enjoystick::voice
