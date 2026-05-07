// VoiceInput.cpp  -  SAPI 5 continuous dictation
// Supports ru-RU (primary) and en-US (secondary).
//
// SAPI dictation grammar loads automatically; no custom grammar XML is needed.
// Recognition events are received via ISpNotifySource::SetNotifyWindowMessage
// and dispatched on the main thread through a message-only HWND, so all
// OnResult/OnState calls happen on whichever thread pumps messages for that
// HWND (typically the main application thread).
//
// Build requirements:
//   sapi.lib, ole32.lib, oleaut32.lib  (listed in voice/CMakeLists.txt)
//   Windows SDK >= 10.0 (SAPI 5.4)

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <wrl/client.h>   // Microsoft::WRL::ComPtr

// SAPI headers (shipped with the Windows SDK)
#include <sapi.h>
#include <sphelper.h>     // SpFindBestToken, SpClearEvent

#pragma comment(lib, "sapi.lib")

#include <enjoystick/voice/VoiceInput.hpp>

#include <atomic>
#include <mutex>
#include <string>
#include <cassert>
#include <cmath>
#include <cstdio>

namespace enjoystick::voice {

using Microsoft::WRL::ComPtr;

// Message posted to the dispatch window by SAPI when events are available
static constexpr UINT WM_VOICE_EVENT = WM_APP + 50;

// ---------------------------------------------------------------------------
// VoiceInputImpl
// ---------------------------------------------------------------------------
class VoiceInputImpl final : public VoiceInput {
public:
    VoiceInputImpl() {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = DispatchWndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"EnjoyStickVoiceDispatch";
        RegisterClassExW(&wc);  // harmless if already registered
        m_dispatchHwnd = CreateWindowExW(
            0, L"EnjoyStickVoiceDispatch", L"", 0,
            0, 0, 0, 0,
            HWND_MESSAGE, nullptr,
            GetModuleHandleW(nullptr), this);
    }

    ~VoiceInputImpl() override {
        StopInternal();
        if (m_dispatchHwnd) { DestroyWindow(m_dispatchHwnd); m_dispatchHwnd = nullptr; }
    }

    // ---- Configuration ---------------------------------------------------
    void SetLanguage(VoiceLanguage lang) override { m_lang = lang; }
    void SetOnResult(OnResultCallback cb) override { m_onResult = std::move(cb); }
    void SetOnState (OnStateCallback  cb) override { m_onState  = std::move(cb); }
    void SetOnError (OnErrorCallback  cb) override { m_onError  = std::move(cb); }

    // ---- Start / Stop ----------------------------------------------------
    bool Start() override {
        if (m_listening.load()) return true;
        return StartInternal();
    }

    void Stop() override { StopInternal(); }

    void CycleLanguage() override {
        const bool wasListening = m_listening.load();
        StopInternal();
        m_lang = (m_lang == VoiceLanguage::Russian)
            ? VoiceLanguage::English
            : VoiceLanguage::Russian;
        if (wasListening) StartInternal();
    }

    // ---- Inspection ------------------------------------------------------
    bool            IsListening()  const noexcept override { return m_listening.load(); }
    VoiceLanguage   GetLanguage()  const noexcept override { return m_lang; }
    VoiceInputState GetState()     const noexcept override {
        std::lock_guard<std::mutex> lk(m_stateMtx);
        return m_state;
    }

private:
    // ---------- SAPI lifetime ---------------------------------------------

    bool StartInternal() {
        HRESULT hr;

        // Try shared recognizer first (uses existing Windows speech profile)
        hr = m_recognizer.Get() ? S_OK
             : m_recognizer.ReleaseAndGetAddressOf(), S_FALSE;
        m_recognizer = nullptr;
        hr = CoCreateInstance(CLSID_SpSharedRecognizer, nullptr,
                              CLSCTX_ALL, IID_PPV_ARGS(m_recognizer.GetAddressOf()));
        if (FAILED(hr)) {
            hr = CoCreateInstance(CLSID_SpInprocRecognizer, nullptr,
                                  CLSCTX_ALL, IID_PPV_ARGS(m_recognizer.GetAddressOf()));
            if (FAILED(hr)) {
                ReportError(L"SAPI: cannot create speech recognizer (HR=0x",  hr);
                return false;
            }
        }

        hr = m_recognizer->CreateRecoContext(m_context.ReleaseAndGetAddressOf());
        if (FAILED(hr)) { ReportError(L"SAPI: CreateRecoContext failed", hr); return false; }

        // Route SAPI events to our message-only window
        hr = m_context->SetNotifyWindowMessage(
            m_dispatchHwnd, WM_VOICE_EVENT, 0, 0);
        if (FAILED(hr)) { ReportError(L"SAPI: SetNotifyWindowMessage failed", hr); return false; }

        const ULONGLONG interestMask =
            SPFEI(SPEI_RECOGNITION)      |
            SPFEI(SPEI_HYPOTHESIS)       |
            SPFEI(SPEI_SOUND_START)      |
            SPFEI(SPEI_SOUND_END)        |
            SPFEI(SPEI_PHRASE_START)     |
            SPFEI(SPEI_RECO_STATE_CHANGE);
        hr = m_context->SetInterest(interestMask, interestMask);
        if (FAILED(hr)) { ReportError(L"SAPI: SetInterest failed", hr); return false; }

        hr = m_context->CreateGrammar(1, m_grammar.ReleaseAndGetAddressOf());
        if (FAILED(hr)) { ReportError(L"SAPI: CreateGrammar failed", hr); return false; }

        hr = m_grammar->LoadDictation(nullptr, SPLO_STATIC);
        if (FAILED(hr)) { ReportError(L"SAPI: LoadDictation failed", hr); return false; }

        hr = m_grammar->SetDictationState(SPRS_ACTIVE);
        if (FAILED(hr)) { ReportError(L"SAPI: SetDictationState failed", hr); return false; }

        TrySetLanguageToken();

        m_recognizer->SetRecoState(SPRST_ACTIVE);

        m_listening.store(true);
        UpdateState([](VoiceInputState& s) {
            s.listening   = true;
            s.recognizing = false;
            s.level       = 0.0f;
            s.partial     = L"";
        });
        return true;
    }

    void StopInternal() {
        if (!m_listening.load()) return;
        m_listening.store(false);

        if (m_grammar) {
            m_grammar->SetDictationState(SPRS_INACTIVE);
            m_grammar = nullptr;
        }
        if (m_context) {
            m_context->SetNotifyWindowMessage(nullptr, 0, 0, 0);
            m_context = nullptr;
        }
        if (m_recognizer) {
            m_recognizer->SetRecoState(SPRST_INACTIVE);
            m_recognizer = nullptr;
        }
        UpdateState([](VoiceInputState& s) {
            s.listening   = false;
            s.recognizing = false;
            s.level       = 0.0f;
            s.partial     = L"";
        });
    }

    void TrySetLanguageToken() {
        if (!m_recognizer) return;
        const LANGID lid = (m_lang == VoiceLanguage::Russian)
            ? MAKELANGID(LANG_RUSSIAN, SUBLANG_DEFAULT)
            : MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
        wchar_t langAttr[64];
        std::swprintf(langAttr, 64, L"Language=%X", static_cast<unsigned>(lid));

        ComPtr<ISpObjectToken> token;
        if (SUCCEEDED(SpFindBestToken(SPCAT_RECOGNIZERS, langAttr, nullptr, &token)) && token) {
            m_recognizer->SetRecognizer(token.Get());
        }
        // Failure is acceptable: shared recognizer uses system default language.
    }

    // ---------- Event processing (main thread via WM_VOICE_EVENT) ---------

    void ProcessSapiEvents() {
        if (!m_context) return;

        SPEVENT evt{};
        ULONG   fetched = 0;
        while (SUCCEEDED(m_context->GetEvents(1, &evt, &fetched)) && fetched > 0) {
            switch (evt.eEventId) {

            case SPEI_SOUND_START:
            case SPEI_PHRASE_START:
                UpdateState([](VoiceInputState& s) {
                    s.recognizing = true;
                    s.level       = 0.70f;  // ramp up on speech start
                });
                break;

            case SPEI_SOUND_END:
                UpdateState([](VoiceInputState& s) {
                    s.recognizing = false;
                    s.level       = 0.0f;
                    s.partial     = L"";
                });
                break;

            case SPEI_HYPOTHESIS: {
                // Partial result: preview text + level estimated from word count
                ISpRecoResult* pResult = reinterpret_cast<ISpRecoResult*>(evt.lParam);
                if (pResult) {
                    SPPHRASE* pPhrase = nullptr;
                    if (SUCCEEDED(pResult->GetPhrase(&pPhrase)) && pPhrase) {
                        std::wstring partial;
                        const ULONG n = pPhrase->Rule.ulCountOfElements;
                        for (ULONG i = 0; i < n; ++i) {
                            if (i > 0) partial += L' ';
                            const wchar_t* word = pPhrase->pElements[i].pszDisplayText;
                            partial += word ? word : L"";
                        }
                        // Estimate audio level from number of recognised words:
                        // 1 word ≈ 0.35, 3 words ≈ 0.65, 5+ words ≈ 0.85
                        const float estLevel = std::min(0.90f,
                            0.30f + static_cast<float>(n) * 0.11f);
                        UpdateState([&partial, estLevel](VoiceInputState& s) {
                            s.partial = partial;
                            s.level   = estLevel;
                        });
                        CoTaskMemFree(pPhrase);
                    }
                }
                break;
            }

            case SPEI_RECOGNITION: {
                ISpRecoResult* pResult = reinterpret_cast<ISpRecoResult*>(evt.lParam);
                if (pResult) {
                    wchar_t* pText = nullptr;
                    const HRESULT hrGet = pResult->GetText(
                        SP_GETWHOLEPHRASE, SP_GETWHOLEPHRASE,
                        TRUE, &pText, nullptr);
                    if (SUCCEEDED(hrGet) && pText && pText[0] != L'\0') {
                        std::wstring result = pText;
                        CoTaskMemFree(pText);
                        UpdateState([](VoiceInputState& s) {
                            s.recognizing = false;
                            s.level       = 0.0f;
                            s.partial     = L"";
                        });
                        if (m_onResult) m_onResult(result);
                    } else {
                        CoTaskMemFree(pText);
                    }
                }
                break;
            }

            default:
                break;
            }
            SpClearEvent(&evt);
        }
    }

    // ---------- State helpers ---------------------------------------------

    template<typename Fn>
    void UpdateState(Fn fn) {
        VoiceInputState snap;
        {
            std::lock_guard<std::mutex> lk(m_stateMtx);
            fn(m_state);
            snap = m_state;
        }
        if (m_onState) m_onState(snap);
    }

    void ReportError(const wchar_t* msg, HRESULT hr = S_OK) {
        if (!m_onError) return;
        if (hr != S_OK) {
            wchar_t buf[256];
            std::swprintf(buf, 256, L"%ls 0x%08X", msg, static_cast<unsigned>(hr));
            m_onError(buf);
        } else {
            m_onError(msg);
        }
    }

    // ---------- Dispatch window -------------------------------------------

    static LRESULT CALLBACK DispatchWndProc(
        HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept
    {
        if (msg == WM_CREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return 0;
        }
        auto* self = reinterpret_cast<VoiceInputImpl*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_VOICE_EVENT && self) {
            self->ProcessSapiEvents();
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    // ---------- Members ---------------------------------------------------
    ComPtr<ISpRecognizer>   m_recognizer;
    ComPtr<ISpRecoContext>  m_context;
    ComPtr<ISpRecoGrammar>  m_grammar;

    HWND              m_dispatchHwnd = nullptr;
    VoiceLanguage     m_lang         = VoiceLanguage::Russian;
    std::atomic<bool> m_listening    { false };
    mutable std::mutex    m_stateMtx;
    VoiceInputState   m_state;

    OnResultCallback  m_onResult;
    OnStateCallback   m_onState;
    OnErrorCallback   m_onError;
};

std::unique_ptr<VoiceInput> VoiceInput::Create() {
    return std::make_unique<VoiceInputImpl>();
}

} // namespace enjoystick::voice
