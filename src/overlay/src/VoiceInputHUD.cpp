#include <enjoystick/overlay/VoiceInputHUD.hpp>
#include "Overlay_Theme.hpp"

#include <d2d1.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

#ifndef M_PI
static constexpr double M_PI = 3.14159265358979323846;
#endif
static constexpr float kPif = static_cast<float>(M_PI);

namespace enjoystick::overlay {

void VoiceInputHUD::Open() {
    if (m_state == State::Visible || m_state == State::Opening) return;
    m_state  = State::Opening;
    m_animT  = 0.0f;
    m_glowPhase = 0.0f;
    m_panelSpring.stiffness  = 560.0f;
    m_panelSpring.damping    = 28.0f;
    m_panelSpring.Snap(0.0f);
    m_panelSpring.SetTarget(1.0f);
    m_micRingSpring.stiffness = 320.0f;
    m_micRingSpring.damping   = 18.0f;
    m_micRingSpring.Snap(0.0f);
    m_levelBarSpring.stiffness = 480.0f;
    m_levelBarSpring.damping   = 24.0f;
    m_levelBarSpring.Snap(0.0f);
}

void VoiceInputHUD::Close() {
    if (m_state == State::Hidden || m_state == State::Closing) return;
    m_state = State::Closing;
    m_panelSpring.SetTarget(0.0f);
}

bool VoiceInputHUD::IsOpen() const noexcept {
    return m_state != State::Hidden;
}

void VoiceInputHUD::Update(const voice::VoiceInputState& vs, float dt) {
    m_vs = vs;

    // Advance springs
    m_panelSpring.Step(dt);
    m_micRingSpring.Step(dt);
    m_levelBarSpring.Step(dt);

    // Open/close animation
    if (m_state == State::Opening) {
        m_animT += dt * kAnimSpeed;
        if (m_animT >= 1.0f) { m_animT = 1.0f; m_state = State::Visible; }
    } else if (m_state == State::Closing) {
        m_animT -= dt * kAnimSpeed;
        if (m_animT <= 0.0f) { m_animT = 0.0f; m_state = State::Hidden; }
    }

    // Glow pulse
    m_glowPhase += dt * kGlowHz * 2.0f * kPif;
    if (m_glowPhase > 2.0f * kPif) m_glowPhase -= 2.0f * kPif;

    // Level smoothing
    m_levelSmooth = m_levelSmooth + (vs.level - m_levelSmooth) *
        std::min(1.0f, dt * (vs.level > m_levelSmooth ? 18.0f : kLevelDecay));
    m_levelBarSpring.SetTarget(m_levelSmooth);

    // Mic ring: pulse when listening idle, larger ring when recognizing
    const float ringTarget = vs.recognizing ? 1.0f
                           : vs.listening   ? 0.5f + 0.15f * std::sin(m_glowPhase)
                           : 0.0f;
    m_micRingSpring.SetTarget(ringTarget);

    // Scroll partial text if too long (simple continuous scroll)
    if (!m_vs.partial.empty()) {
        m_partialScrollX += dt * 28.0f;
    } else {
        m_partialScrollX = 0.0f;
    }
}

void VoiceInputHUD::Draw(
    void* renderTargetPtr, void* dwriteFactoryPtr,
    float dpiScale, float screenW, float screenH) const
{
    if (m_state == State::Hidden) return;
    const float ease = std::clamp(m_animT, 0.0f, 1.0f);
    if (ease <= 0.001f) return;
    if (!renderTargetPtr) return;

    auto* rt     = static_cast<ID2D1RenderTarget*>(renderTargetPtr);
    auto* dwrite = static_cast<IDWriteFactory*>(dwriteFactoryPtr);

    const float s = dpiScale;

    // ---- Panel geometry (centered, bottom-third of screen)
    const float pw  = 340.0f * s;
    const float ph  = 120.0f * s;
    const float cr  = 18.0f  * s;
    const float px  = (screenW - pw) * 0.5f;
    const float py  = screenH * 0.62f;
    const float cx  = px + pw * 0.5f;
    const float cy  = py + ph * 0.5f;

    // Scale-pop from centre
    const float scCl = std::clamp(m_panelSpring.value, 0.0f, 1.2f);

    D2D1_MATRIX_3X2_F oldTransform;
    rt->GetTransform(&oldTransform);
    D2D1_MATRIX_3X2_F scaleT = D2D1::Matrix3x2F::Scale(
        D2D1::SizeF(scCl, scCl),
        D2D1::Point2F(cx, cy));
    rt->SetTransform(scaleT * oldTransform);

    // ---- Background
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceBase(0.94f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(px,py,px+pw,py+ph),cr,cr};
                 rt->FillRoundedRectangle(rr, b.Get()); }
    }
    // ---- Border glow (bright when recognizing, soft pulse when idle)
    {
        const float glowA = m_vs.recognizing
            ? 0.90f
            : 0.35f + 0.20f * std::sin(m_glowPhase);
        const D2D1_COLOR_F borderCol = m_vs.recognizing
            ? Tok::GoldBright(glowA * ease)
            : Tok::GoldMid(glowA * ease);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(borderCol, b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(px,py,px+pw,py+ph),cr,cr};
                 rt->DrawRoundedRectangle(rr, b.Get(), 1.5f * s); }
    }

    // ---- Microphone icon (left side of panel)
    const float micCX = px + 52.0f * s;
    const float micCY = cy;
    const float micR  = 16.0f * s;

    // Outer glow ring (spring-driven)
    {
        const float ringR = micR + m_micRingSpring.value * 12.0f * s;
        const float ringA = 0.30f + m_micRingSpring.value * 0.55f;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(
            m_vs.recognizing ? Tok::GoldBright(ringA * ease) : Tok::GoldMid(ringA * ease),
            b.GetAddressOf());
        if (b) rt->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(micCX, micCY), ringR, ringR), b.Get());
    }
    // Mic body circle
    {
        const D2D1_COLOR_F micCol = m_vs.listening
            ? Tok::GoldMid(0.92f * ease)
            : Tok::ChromeMute(0.55f * ease);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(micCol, b.GetAddressOf());
        if (b) rt->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(micCX, micCY), micR, micR), b.Get());
    }
    // Mic symbol (capsule body + stand line)
    {
        const float mw = 6.0f * s;
        const float mh = 11.0f * s;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceBase(0.95f * ease), b.GetAddressOf());
        if (b) {
            D2D1_ROUNDED_RECT cap{ D2D1::RectF(
                micCX - mw*0.5f, micCY - mh*0.5f,
                micCX + mw*0.5f, micCY + mh*0.5f),
                mw*0.5f, mw*0.5f };
            rt->FillRoundedRectangle(cap, b.Get());
            rt->DrawLine(
                D2D1::Point2F(micCX, micCY + mh*0.5f),
                D2D1::Point2F(micCX, micCY + mh*0.5f + 3.0f*s),
                b.Get(), 1.5f*s);
        }
    }

    // ---- Language badge (top-right of mic circle)
    if (dwrite && !m_langLabel.empty()) {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
        dwrite->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 9.5f * s, L"en-us", fmt.GetAddressOf());
        if (fmt) {
            const float bx = micCX + micR - 2.0f*s;
            const float by = micCY - micR - 2.0f*s;
            const float bw = 20.0f * s;
            const float bh = 12.0f * s;
            { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
              rt->CreateSolidColorBrush(Tok::GoldDeep(0.82f * ease), b.GetAddressOf());
              if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(bx,by,bx+bw,by+bh),3.0f*s,3.0f*s};
                       rt->FillRoundedRectangle(rr, b.Get()); } }
            { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
              rt->CreateSolidColorBrush(Tok::GoldBright(0.97f * ease), b.GetAddressOf());
              if (b) {
                  fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                  fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                  rt->DrawText(m_langLabel.c_str(),
                      static_cast<UINT32>(m_langLabel.size()),
                      fmt.Get(), D2D1::RectF(bx,by,bx+bw,by+bh), b.Get());
              }
            }
        }
    }

    // ---- Status text + partial result (right of mic)
    if (dwrite) {
        const float tx  = px + 90.0f * s;
        const float tw  = pw - 90.0f * s - 16.0f * s;

        const wchar_t* statusStr = m_vs.recognizing ? L"Listening\u2026"
                                 : m_vs.listening    ? L"Say something\u2026"
                                 : L"Voice input";
        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtStatus;
        dwrite->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 13.0f * s, L"ru-ru", fmtStatus.GetAddressOf());
        if (fmtStatus) {
            fmtStatus->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(
                m_vs.recognizing ? Tok::GoldHi(0.96f*ease) : Tok::ChromeHi(0.75f*ease),
                b.GetAddressOf());
            if (b) rt->DrawText(statusStr,
                static_cast<UINT32>(std::wcslen(statusStr)),
                fmtStatus.Get(),
                D2D1::RectF(tx, cy - 28.0f*s, tx+tw, cy), b.Get());
        }

        // Partial result with horizontal clip
        if (!m_vs.partial.empty()) {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtP;
            dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_ITALIC,
                DWRITE_FONT_STRETCH_NORMAL, 12.0f * s, L"ru-ru", fmtP.GetAddressOf());
            if (fmtP) {
                rt->PushAxisAlignedClip(
                    D2D1::RectF(tx, cy, tx+tw, cy + 24.0f*s),
                    D2D1_ANTIALIAS_MODE_ALIASED);
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(Tok::GoldMid(0.88f*ease), b.GetAddressOf());
                if (b) rt->DrawText(
                    m_vs.partial.c_str(),
                    static_cast<UINT32>(m_vs.partial.size()),
                    fmtP.Get(),
                    D2D1::RectF(tx - m_partialScrollX, cy, tx + tw*3.0f, cy + 24.0f*s),
                    b.Get());
                rt->PopAxisAlignedClip();
            }
        }

        // Hint line: cancel + language cycle
        static constexpr wchar_t kHint[] = L"\u25C6 cancel  \u2665 switch lang";
        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtHint;
        dwrite->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 9.5f * s, L"en-us", fmtHint.GetAddressOf());
        if (fmtHint) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(Tok::ChromeMute(0.45f*ease), b.GetAddressOf());
            if (b) rt->DrawText(
                kHint,
                static_cast<UINT32>(std::wcslen(kHint)),
                fmtHint.Get(),
                D2D1::RectF(tx, cy + 26.0f*s, tx+tw, cy + 42.0f*s),
                b.Get());
        }
    }

    // ---- VU bar (bottom of panel, full width minus padding)
    {
        const float barH   = 3.5f * s;
        const float barY   = py + ph - barH - 6.0f * s;
        const float barX0  = px + 12.0f * s;
        const float barX1  = px + pw  - 12.0f * s;
        const float barW   = barX1 - barX0;
        const float fillW  = barW * std::clamp(m_levelBarSpring.value, 0.0f, 1.0f);

        { Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
          rt->CreateSolidColorBrush(Tok::SurfaceSunken(0.60f * ease), b.GetAddressOf());
          if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(barX0,barY,barX1,barY+barH),barH*0.5f,barH*0.5f};
                   rt->FillRoundedRectangle(rr, b.Get()); } }
        if (fillW > 1.0f) {
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(Tok::GoldMid(0.80f * ease), b.GetAddressOf());
            if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(barX0,barY,barX0+fillW,barY+barH),barH*0.5f,barH*0.5f};
                     rt->FillRoundedRectangle(rr, b.Get()); }
        }
    }

    rt->SetTransform(oldTransform);
}

} // namespace enjoystick::overlay
