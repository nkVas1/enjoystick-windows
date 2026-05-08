// VoiceInput.cpp  -  SAPI 5 continuous dictation
// Supports ru-RU (primary) and en-US (secondary).
//
// SAPI dictation grammar loads automatically; no custom grammar XML is needed.
// Recognition events are received via ISpNotifySource::SetNotifyWindowMessage
// and dispatched on the main thread through a message-only HWND, so all
// OnResult/OnState calls happen on the thread that owns the HWND (main thread).
//
// Build requirements:
//   sapi.lib, ole32.lib, oleaut32.lib  (listed in voice/CMakeLists.txt)
//   CoInitializeEx() is called automatically inside Start() on first use;
//   it is safe to call it again if the host already initialised COM.

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <wrl/client.h>   // Microsoft::WRL::ComPtr  (no ATL required)

// SAPI headers (shipped with the Windows SDK).
// NOTE: sphelper.h is intentionally NOT included: it hard-includes atlbase.h
//       which requires the ATL/MFC optional workload.  We provide inline
//       replacements for the two helpers we need (SpClearEvent, SpFindBestToken).
#include <sapi.h>

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

// Message posted to the dispatch window by SAPI when events are available.
static constexpr UINT WM_VOICE_EVENT = WM_APP + 50;

// SAPI-specific HRESULTs not in public SDK headers
static constexpr HRESULT kSPERR_NOT_FOUND              = static_cast<HRESULT>(0x80045003L);
static constexpr HRESULT kSPERR_SHARED_ENGINE_DISABLED = static_cast<HRESULT>(0x80045077L);
// Generic "shared engine unavailable" code that also appears on some configs
static constexpr HRESULT kSPERR_RECOGNIZER_NOT_FOUND   = static_cast<HRESULT>(0x80045014L);

// ---------------------------------------------------------------------------
// ATL-free replacements for sphelper.h utilities
// ---------------------------------------------------------------------------

/// Free any CoTask-allocated strings inside a SPEVENT, then zero it.
static inline void SpClearEventInline(SPEVENT* pEvt) noexcept {
    if (!pEvt) return;
    if (SPET_LPARAM_IS_STRING ==
        static_cast<SPEVENTLPARAMTYPE>(HIWORD(pEvt->wParam)))
    {
        CoTaskMemFree(reinterpret_cast<void*>(pEvt->lParam));
    }
    *pEvt = SPEVENT{};
}

/// Find the best SAPI token matching pszReqAttribs in the given category.
static HRESULT SpFindBestTokenInCategory(
    const wchar_t*   pszCategoryId,
    const wchar_t*   pszReqAttribs,
    const wchar_t*   pszOptAttribs,
    ISpObjectToken** ppToken) noexcept
{
    if (!ppToken) return E_POINTER;
    *ppToken = nullptr;

    ComPtr<ISpObjectTokenCategory> cat;
    HRESULT hr = CoCreateInstance(
        CLSID_SpObjectTokenCategory, nullptr,
        CLSCTX_ALL, IID_PPV_ARGS(cat.GetAddressOf()));
    if (FAILED(hr)) return hr;

    hr = cat->SetId(pszCategoryId, FALSE);
    if (FAILED(hr)) return hr;

    ComPtr<IEnumSpObjectTokens> enumTok;
    hr = cat->EnumTokens(pszReqAttribs, pszOptAttribs, enumTok.GetAddressOf());
    if (FAILED(hr)) return hr;

    ULONG fetched = 0;
    hr = enumTok->Next(1, ppToken, &fetched);
    if (SUCCEEDED(hr) && fetched == 1 && *ppToken) return S_OK;
    if (*ppToken) { (*ppToken)->Release(); *ppToken = nullptr; }
    return kSPERR_NOT_FOUND;
}

static HRESULT SpFindBestTokenLocal(
    const wchar_t*   pszReqAttribs,
    const wchar_t*   pszOptAttribs,
    ISpObjectToken** ppToken) noexcept
{
    return SpFindBestTokenInCategory(
        SPCAT_RECOGNIZERS, pszReqAttribs, pszOptAttribs, ppToken);
}

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
        if (m_comInitialised) { CoUninitialize(); m_comInitialised = false; }
    }

    void SetLanguage(VoiceLanguage lang) override { m_lang = lang; }
    void SetOnResult(OnResultCallback cb) override { m_onResult = std::move(cb); }
    void SetOnState (OnStateCallback  cb) override { m_onState  = std::move(cb); }
    void SetOnError (OnErrorCallback  cb) override { m_onError  = std::move(cb); }

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

    bool            IsListening()  const noexcept override { return m_listening.load(); }
    VoiceLanguage   GetLanguage()  const noexcept override { return m_lang; }
    VoiceInputState GetState()     const noexcept override {
        std::lock_guard<std::mutex> lk(m_stateMtx);
        return m_state;
    }

private:
    // -------------------------------------------------------------------------
    // EnsureComInitialised
    // -------------------------------------------------------------------------
    bool EnsureComInitialised() {
        if (m_comInitialised) return true;
        const HRESULT hr = CoInitializeEx(
            nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (SUCCEEDED(hr)) {
            m_comInitialised = true;
            return true;
        }
        if (hr == RPC_E_CHANGED_MODE) {
            // COM already init'd with a different model on this thread; SAPI is fine with MTA.
            return true;
        }
        ReportError(L"COM init failed before SAPI start", hr);
        return false;
    }

    // -------------------------------------------------------------------------
    // TryBindAudioInput
    //
    // For SpInprocRecognizer we must explicitly bind an audio input device;
    // otherwise it may fail to start recording on some Windows configurations.
    // We enumerate SPCAT_AUDIOIN and set the first available token.
    // Failure is non-fatal: the recognizer will attempt its own default.
    // -------------------------------------------------------------------------
    void TryBindAudioInput() noexcept {
        if (!m_recognizer) return;
        ComPtr<ISpObjectToken> audioToken;
        if (SUCCEEDED(SpFindBestTokenInCategory(
                SPCAT_AUDIOIN, nullptr, nullptr, audioToken.GetAddressOf()))
            && audioToken)
        {
            m_recognizer->SetInput(audioToken.Get(), TRUE);
        }
    }

    // -------------------------------------------------------------------------
    // StartInternal
    //
    // Strategy:
    //  1. Try SpSharedRecognizer + CreateRecoContext.
    //     This works when Windows Speech Recognition service is running.
    //  2. If CreateRecoContext returns SPERR_SHARED_ENGINE_DISABLED (0x80045077)
    //     or any failure, release and retry with SpInprocRecognizer.
    //     InprocRecognizer runs fully in-process, needs no Windows service,
    //     but requires an explicit audio input binding.
    // -------------------------------------------------------------------------
    bool StartInternal() {
        if (!EnsureComInitialised()) return false;

        m_grammar    = nullptr;
        m_context    = nullptr;
        m_recognizer = nullptr;

        HRESULT hr = TryStartWithRecognizerClsid(
            CLSID_SpSharedRecognizer, /*bindAudio=*/false);

        if (FAILED(hr)) {
            // Shared engine unavailable or disabled — fall back to in-process.
            m_grammar    = nullptr;
            m_context    = nullptr;
            m_recognizer = nullptr;

            hr = TryStartWithRecognizerClsid(
                CLSID_SpInprocRecognizer, /*bindAudio=*/true);

            if (FAILED(hr)) {
                // Both failed — nothing more we can do.
                return false;
            }
        }

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

    // Returns S_OK on full success, or the first HRESULT failure.
    // On failure the caller must release m_grammar/m_context/m_recognizer.
    HRESULT TryStartWithRecognizerClsid(REFCLSID clsid, bool bindAudio) noexcept {
        HRESULT hr = CoCreateInstance(clsid, nullptr,
            CLSCTX_ALL, IID_PPV_ARGS(m_recognizer.ReleaseAndGetAddressOf()));
        if (FAILED(hr)) {
            if (clsid == CLSID_SpSharedRecognizer) {
                // Don't report error here; caller will try inproc silently.
            } else {
                ReportError(L"SAPI: cannot create in-process recognizer (SAPI may not be installed)", hr);
            }
            return hr;
        }

        if (bindAudio) TryBindAudioInput();

        hr = m_recognizer->CreateRecoContext(m_context.ReleaseAndGetAddressOf());
        if (FAILED(hr)) {
            if (clsid == CLSID_SpSharedRecognizer) {
                // Silently let the caller retry with inproc.
            } else {
                ReportError(L"SAPI: CreateRecoContext (in-process)", hr);
            }
            return hr;
        }

        hr = m_context->SetNotifyWindowMessage(m_dispatchHwnd, WM_VOICE_EVENT, 0, 0);
        if (FAILED(hr)) { ReportError(L"SAPI: SetNotifyWindowMessage", hr); return hr; }

        const ULONGLONG interest =
            SPFEI(SPEI_RECOGNITION)      |
            SPFEI(SPEI_HYPOTHESIS)       |
            SPFEI(SPEI_SOUND_START)      |
            SPFEI(SPEI_SOUND_END)        |
            SPFEI(SPEI_PHRASE_START)     |
            SPFEI(SPEI_RECO_STATE_CHANGE);
        hr = m_context->SetInterest(interest, interest);
        if (FAILED(hr)) { ReportError(L"SAPI: SetInterest", hr); return hr; }

        hr = m_context->CreateGrammar(1, m_grammar.ReleaseAndGetAddressOf());
        if (FAILED(hr)) { ReportError(L"SAPI: CreateGrammar", hr); return hr; }

        hr = m_grammar->LoadDictation(nullptr, SPLO_STATIC);
        if (FAILED(hr)) {
            ReportError(
                L"SAPI: LoadDictation failed \u2014 install a Speech Recognition language pack",
                hr);
            return hr;
        }

        hr = m_grammar->SetDictationState(SPRS_ACTIVE);
        if (FAILED(hr)) { ReportError(L"SAPI: SetDictationState", hr); return hr; }

        return S_OK;
    }

    void StopInternal() {
        if (!m_listening.load()) return;
        m_listening.store(false);

        if (m_grammar) {
            m_grammar->SetDictationState(SPRS_INACTIVE);
            m_grammar = nullptr;
        }
        if (m_context) {
            if (m_dispatchHwnd)
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
        wchar_t attr[64];
        std::swprintf(attr, 64, L"Language=%X", static_cast<unsigned>(lid));
        ComPtr<ISpObjectToken> token;
        if (SUCCEEDED(SpFindBestTokenLocal(attr, nullptr, token.GetAddressOf())) && token)
            m_recognizer->SetRecognizer(token.Get());
        // Failure is acceptable: recognizer uses system default language.
    }

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
                    s.level       = 0.70f;
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
                        const float lvl = std::min(0.90f, 0.30f + static_cast<float>(n) * 0.11f);
                        UpdateState([&partial, lvl](VoiceInputState& s) {
                            s.partial = partial;
                            s.level   = lvl;
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
                    const ULONG kWhole = static_cast<ULONG>(SP_GETWHOLEPHRASE);
                    if (SUCCEEDED(pResult->GetText(kWhole, kWhole, TRUE, &pText, nullptr))
                        && pText && pText[0] != L'\0')
                    {
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

            default: break;
            }
            SpClearEventInline(&evt);
        }
    }

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
        wchar_t buf[256];
        if (FAILED(hr))
            std::swprintf(buf, 256, L"%ls (HR=0x%08X)", msg, static_cast<unsigned>(hr));
        else
            std::swprintf(buf, 256, L"%ls", msg);
        m_onError(buf);
    }

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

    ComPtr<ISpRecognizer>   m_recognizer;
    ComPtr<ISpRecoContext>  m_context;
    ComPtr<ISpRecoGrammar>  m_grammar;

    HWND               m_dispatchHwnd  = nullptr;
    VoiceLanguage      m_lang          = VoiceLanguage::Russian;
    std::atomic<bool>  m_listening     { false };
    bool               m_comInitialised = false;
    mutable std::mutex m_stateMtx;
    VoiceInputState    m_state;

    OnResultCallback   m_onResult;
    OnStateCallback    m_onState;
    OnErrorCallback    m_onError;
};

std::unique_ptr<VoiceInput> VoiceInput::Create() {
    return std::make_unique<VoiceInputImpl>();
}

} // namespace enjoystick::voice
