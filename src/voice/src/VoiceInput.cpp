// VoiceInput.cpp  –  SAPI 5 continuous dictation
// Supports ru-RU (primary) and en-US (secondary).
//
// SAPI dictation grammar loads automatically; no custom grammar XML is needed.
// Recognition events are received via ISpNotifySource::SetNotifySink and
// dispatched on the calling (main) thread through a queued approach:
// the recognition event callback posts a WM_APP message to a helper HWND
// created on the main thread, so all OnResult/OnState calls happen safely.
//
// Build requirements:
//   sapi.lib  (add to enjoystick_app target's PRIVATE link libs)

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <atlbase.h>  // CComPtr / CComQIPtr

// SAPI headers (shipped with the Windows SDK)
#include <sapi.h>
#include <sphelper.h>   // SpFindBestToken, etc.

#pragma comment(lib, "sapi.lib")

#include <enjoystick/voice/VoiceInput.hpp>

#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <cassert>
#include <cmath>

namespace enjoystick::voice {

// ---------------------------------------------------------------------------
// Language profiles
// ---------------------------------------------------------------------------
struct LangProfile {
    const wchar_t* bcp47;      // BCP-47 tag used by SAPI
    const wchar_t* lcid_str;   // language ID as decimal string for SpFindBestToken
    LANGID          langid;
};

static constexpr LangProfile kLangProfiles[] = {
    { L"ru-RU", L"409",  MAKELANGID(LANG_RUSSIAN,  SUBLANG_DEFAULT) },
    { L"en-US", L"409",  MAKELANGID(LANG_ENGLISH,  SUBLANG_ENGLISH_US) },
};

// Message posted to the dispatch window
static constexpr UINT WM_VOICE_RESULT  = WM_APP + 50;
static constexpr UINT WM_VOICE_STATE   = WM_APP + 51;
static constexpr UINT WM_VOICE_ERROR   = WM_APP + 52;

// ---------------------------------------------------------------------------
// VoiceInputImpl
// ---------------------------------------------------------------------------
class VoiceInputImpl final : public VoiceInput {
public:
    VoiceInputImpl() {
        // Create an invisible message-only window for safe cross-thread dispatch
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = DispatchWndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"VoiceInputDispatch";
        RegisterClassExW(&wc);  // ignore duplicate registration
        m_dispatchHwnd = CreateWindowExW(
            0, L"VoiceInputDispatch", L"", 0,
            0, 0, 0, 0,
            HWND_MESSAGE, nullptr,
            GetModuleHandleW(nullptr), this);
        if (m_dispatchHwnd) {
            SetWindowLongPtrW(m_dispatchHwnd, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(this));
        }
    }

    ~VoiceInputImpl() override {
        StopInternal();
        if (m_dispatchHwnd) DestroyWindow(m_dispatchHwnd);
    }

    // ---- Configuration ---------------------------------------------------
    void SetLanguage(VoiceLanguage lang) override { m_lang = lang; }
    void SetOnResult(OnResultCallback cb) override { m_onResult = std::move(cb); }
    void SetOnState (OnStateCallback  cb) override { m_onState  = std::move(cb); }
    void SetOnError (OnErrorCallback  cb) override { m_onError  = std::move(cb); }

    // ---- Start / Stop ---------------------------------------------------
    bool Start() override {
        if (m_listening.load()) return true;
        return StartInternal();
    }

    void Stop() override {
        StopInternal();
    }

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
    // ---------- SAPI helpers ----------------------------------------------

    bool StartInternal() {
        HRESULT hr = S_OK;

        hr = m_recognizer.CoCreateInstance(CLSID_SpSharedRecognizer);
        if (FAILED(hr)) {
            hr = m_recognizer.CoCreateInstance(CLSID_SpInprocRecognizer);
            if (FAILED(hr)) { ReportError(L"SAPI: cannot create recognizer"); return false; }
        }

        hr = m_recognizer->CreateRecoContext(m_context.ReleaseAndGetAddressOf());
        if (FAILED(hr)) { ReportError(L"SAPI: cannot create reco context"); return false; }

        // Route recognition events to our dispatch window via SetNotifyWindowMessage
        hr = m_context->SetNotifyWindowMessage(
            m_dispatchHwnd, WM_VOICE_RESULT, 0, 0);
        if (FAILED(hr)) { ReportError(L"SAPI: SetNotifyWindowMessage failed"); return false; }

        hr = m_context->SetInterest(
            SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_SOUND_START) | SPFEI(SPEI_SOUND_END)
            | SPFEI(SPEI_HYPOTHESIS) | SPFEI(SPEI_RECO_STATE_CHANGE),
            SPFEI(SPEI_RECOGNITION) | SPFEI(SPEI_SOUND_START) | SPFEI(SPEI_SOUND_END)
            | SPFEI(SPEI_HYPOTHESIS) | SPFEI(SPEI_RECO_STATE_CHANGE));
        if (FAILED(hr)) { ReportError(L"SAPI: SetInterest failed"); return false; }

        // Load dictation grammar
        hr = m_context->CreateGrammar(1, m_grammar.ReleaseAndGetAddressOf());
        if (FAILED(hr)) { ReportError(L"SAPI: CreateGrammar failed"); return false; }

        hr = m_grammar->LoadDictation(nullptr, SPLO_STATIC);
        if (FAILED(hr)) { ReportError(L"SAPI: LoadDictation failed"); return false; }

        hr = m_grammar->SetDictationState(SPRS_ACTIVE);
        if (FAILED(hr)) { ReportError(L"SAPI: SetDictationState failed"); return false; }

        // Set recognition language via audio input object language
        // (Full token selection is not required for shared recognizer;
        //  for inproc we try to select the best matching token)
        TrySetLanguageToken();

        // Set audio state to active
        m_recognizer->SetRecoState(SPRST_ACTIVE);

        m_listening.store(true);
        UpdateState([](VoiceInputState& s) { s.listening = true; s.recognizing = false; });
        return true;
    }

    void StopInternal() {
        if (!m_listening.load()) return;
        m_listening.store(false);

        if (m_grammar) {
            m_grammar->SetDictationState(SPRS_INACTIVE);
            m_grammar.Reset();
        }
        if (m_context) {
            m_context->SetNotifyWindowMessage(nullptr, 0, 0, 0);
            m_context.Reset();
        }
        if (m_recognizer) {
            m_recognizer->SetRecoState(SPRST_INACTIVE);
            m_recognizer.Reset();
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
        // Build attribute query for the desired language
        const bool isRu = (m_lang == VoiceLanguage::Russian);
        // Language attribute value is the hex LANGID
        wchar_t langAttr[64];
        const LANGID lid = isRu
            ? MAKELANGID(LANG_RUSSIAN, SUBLANG_DEFAULT)
            : MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
        std::swprintf(langAttr, 64, L"Language=%X", static_cast<unsigned>(lid));

        CComPtr<ISpObjectToken> token;
        HRESULT hr = SpFindBestToken(SPCAT_RECOGNIZERS, langAttr, nullptr, &token);
        if (SUCCEEDED(hr) && token) {
            m_recognizer->SetRecognizer(token);
        }
        // Even if token selection fails the shared recognizer will use
        // whatever language profile Windows has active — acceptable fallback.
    }

    // ---- Event processing (called on main thread via WM_VOICE_RESULT) ----

    void ProcessSapiEvents() {
        if (!m_context) return;

        SPEVENT evt{};
        ULONG   fetched = 0;
        while (SUCCEEDED(m_context->GetEvents(1, &evt, &fetched)) && fetched > 0) {
            switch (evt.eEventId) {
            case SPEI_SOUND_START:
                UpdateState([](VoiceInputState& s) { s.recognizing = true; });
                break;

            case SPEI_SOUND_END:
                UpdateState([](VoiceInputState& s) {
                    s.recognizing = false;
                    s.level       = 0.0f;
                    s.partial     = L"";
                });
                break;

            case SPEI_HYPOTHESIS: {
                // Partial result — update the preview text
                ISpRecoResult* pResult = reinterpret_cast<ISpRecoResult*>(evt.lParam);
                if (pResult) {
                    SPPHRASE* pPhrase = nullptr;
                    if (SUCCEEDED(pResult->GetPhrase(&pPhrase)) && pPhrase) {
                        if (pPhrase->pElements) {
                            std::wstring partial;
                            for (ULONG i = 0; i < pPhrase->Rule.ulCountOfElements; ++i) {
                                if (i > 0) partial += L' ';
                                partial += pPhrase->pElements[i].pszDisplayText
                                    ? pPhrase->pElements[i].pszDisplayText
                                    : L"";
                            }
                            UpdateState([&partial](VoiceInputState& s) {
                                s.partial = partial;
                            });
                        }
                        CoTaskMemFree(pPhrase);
                    }
                }
                break;
            }

            case SPEI_RECOGNITION: {
                ISpRecoResult* pResult = reinterpret_cast<ISpRecoResult*>(evt.lParam);
                if (pResult) {
                    wchar_t* pText = nullptr;
                    if (SUCCEEDED(pResult->GetText(SP_GETWHOLEPHRASE, SP_GETWHOLEPHRASE,
                                                   TRUE, &pText, nullptr))
                        && pText && pText[0] != L'\0')
                    {
                        std::wstring result = pText;
                        CoTaskMemFree(pText);

                        UpdateState([](VoiceInputState& s) {
                            s.recognizing = false;
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
            ::SpClearEvent(&evt);
        }
    }

    // ---- State helpers ---------------------------------------------------

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

    void ReportError(const wchar_t* msg) {
        if (m_onError) m_onError(msg);
    }

    // ---- Dispatch window -------------------------------------------------

    static LRESULT CALLBACK DispatchWndProc(
        HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) noexcept
    {
        auto* self = reinterpret_cast<VoiceInputImpl*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (msg == WM_CREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return 0;
        }
        if (msg == WM_VOICE_RESULT && self) {
            self->ProcessSapiEvents();
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    // ---- Members ---------------------------------------------------------
    CComPtr<ISpRecognizer>    m_recognizer;
    CComPtr<ISpRecoContext>   m_context;
    CComPtr<ISpRecoGrammar>   m_grammar;

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
