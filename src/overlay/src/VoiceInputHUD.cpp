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
    m_wavePhase = 0.0f;
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
    m_levelSmooth = 0.0f;
    m_partialScrollX = 0.0f;
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

    m_panelSpring.Step(dt);
    m_micRingSpring.Step(dt);
    m_levelBarSpring.Step(dt);

    if (m_state == State::Opening) {
        m_animT += dt * kAnimSpeed;
        if (m_animT >= 1.0f) { m_animT = 1.0f; m_state = State::Visible; }
    } else if (m_state == State::Closing) {
        m_animT -= dt * kAnimSpeed;
        if (m_animT <= 0.0f) { m_animT = 0.0f; m_state = State::Hidden; }
    }

    m_glowPhase += dt * kGlowHz * 2.0f * kPif;
    if (m_glowPhase > 2.0f * kPif) m_glowPhase -= 2.0f * kPif;

    // Wave animation — faster when recognizing
    const float waveSpeed = vs.recognizing ? 4.5f : (vs.listening ? 2.5f : 1.0f);
    m_wavePhase += dt * waveSpeed;
    if (m_wavePhase > 2.0f * kPif) m_wavePhase -= 2.0f * kPif;

    // Level smoothing: fast attack, moderate decay
    const float targetLevel = vs.level;
    const float rate = (targetLevel > m_levelSmooth) ? 22.0f : kLevelDecay;
    m_levelSmooth += (targetLevel - m_levelSmooth) * std::min(1.0f, dt * rate);
    m_levelBarSpring.SetTarget(m_levelSmooth);

    // Mic ring pulse
    const float ringTarget = vs.recognizing ? 1.0f
                           : vs.listening   ? 0.5f + 0.18f * std::sin(m_glowPhase)
                           : 0.0f;
    m_micRingSpring.SetTarget(ringTarget);

    // Scroll partial text
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

    // ---- Panel geometry (centered, lower third of screen)
    const float pw  = 480.0f * s;
    const float ph  = 210.0f * s;
    const float cr  = 20.0f  * s;
    const float px  = (screenW - pw) * 0.5f;
    const float py  = screenH * 0.60f;
    const float cx  = px + pw * 0.5f;
    const float cy  = py + ph * 0.5f;

    // Scale-pop from centre
    const float scCl = std::clamp(m_panelSpring.value, 0.0f, 1.15f);

    D2D1_MATRIX_3X2_F oldTransform;
    rt->GetTransform(&oldTransform);
    rt->SetTransform(
        D2D1::Matrix3x2F::Scale(D2D1::SizeF(scCl, scCl), D2D1::Point2F(cx, cy))
        * oldTransform);

    // ---- Background with subtle inner gradient illusion via two rects
    {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(Tok::SurfaceBase(0.96f * ease), b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(px,py,px+pw,py+ph),cr,cr};
                 rt->FillRoundedRectangle(rr, b.Get()); }
    }
    // Top accent stripe
    {
        const float stripeH = 3.0f * s;
        const D2D1_COLOR_F stripeCol = m_vs.recognizing
            ? Tok::GoldBright(0.90f * ease)
            : m_vs.listening
                ? Tok::GoldMid(0.70f + 0.20f * std::sin(m_glowPhase) * ease)
                : Tok::ChromeMute(0.30f * ease);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(stripeCol, b.GetAddressOf());
        if (b) {
            // Fill top rounded corners area only
            D2D1_ROUNDED_RECT rr{D2D1::RectF(px,py,px+pw,py+stripeH+cr),cr,cr};
            rt->FillRoundedRectangle(rr, b.Get());
            // Cover lower half of stripe with bg color to make it flat
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> bg;
            rt->CreateSolidColorBrush(Tok::SurfaceBase(0.96f * ease), bg.GetAddressOf());
            if (bg) rt->FillRectangle(D2D1::RectF(px, py+stripeH, px+pw, py+stripeH+cr), bg.Get());
        }
    }
    // Border — glows brighter when recognizing
    {
        const float glowA = m_vs.recognizing
            ? 0.92f
            : m_vs.listening
                ? 0.40f + 0.22f * std::sin(m_glowPhase)
                : 0.20f;
        const D2D1_COLOR_F borderCol = m_vs.recognizing
            ? Tok::GoldBright(glowA * ease)
            : m_vs.listening
                ? Tok::GoldMid(glowA * ease)
                : Tok::ChromeMute(glowA * ease);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(borderCol, b.GetAddressOf());
        if (b) { D2D1_ROUNDED_RECT rr{D2D1::RectF(px,py,px+pw,py+ph),cr,cr};
                 rt->DrawRoundedRectangle(rr, b.Get(), 1.6f * s); }
    }

    // =========================================================
    // LEFT SECTION: Microphone visualizer (animated ring + icon)
    // =========================================================
    const float micCX = px + 68.0f * s;
    const float micCY = py + ph * 0.44f;
    const float micR  = 28.0f * s;

    // Outer ambient glow (soft, large)
    if (m_vs.listening || m_vs.recognizing) {
        const float glowR = micR + 22.0f * s + m_micRingSpring.value * 14.0f * s;
        const float glowA = (m_vs.recognizing ? 0.18f : 0.10f)
                          + m_micRingSpring.value * 0.12f;
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        const D2D1_COLOR_F gc = m_vs.recognizing
            ? Tok::GoldBright(glowA * ease)
            : Tok::GoldMid(glowA * ease);
        rt->CreateSolidColorBrush(gc, b.GetAddressOf());
        if (b) rt->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(micCX, micCY), glowR, glowR), b.Get());
    }

    // Animated ring (spring-driven radius, state-coded color)
    {
        const float ringThick = 2.2f * s;
        const float ringR = micR + 8.0f * s + m_micRingSpring.value * 10.0f * s;
        const float ringA = 0.20f + m_micRingSpring.value * 0.65f;
        const D2D1_COLOR_F ringCol = m_vs.recognizing
            ? Tok::GoldBright(ringA * ease)
            : m_vs.listening
                ? Tok::GoldMid(ringA * ease)
                : Tok::ChromeMute(0.25f * ease);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(ringCol, b.GetAddressOf());
        if (b) rt->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(micCX, micCY), ringR, ringR),
            b.Get(), ringThick);
    }

    // Mic body circle (color = muted/listening/recognizing)
    {
        const D2D1_COLOR_F micBg = m_vs.recognizing
            ? Tok::GoldDeep(0.90f * ease)
            : m_vs.listening
                ? Tok::GoldDeep(0.65f * ease)
                : Tok::SurfaceSunken(0.80f * ease);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(micBg, b.GetAddressOf());
        if (b) rt->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(micCX, micCY), micR, micR), b.Get());
    }
    // Mic body border
    {
        const float bA = m_vs.recognizing ? 0.70f : (m_vs.listening ? 0.45f : 0.20f);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(
            m_vs.recognizing ? Tok::GoldHi(bA * ease) : Tok::GoldShadow(bA * ease),
            b.GetAddressOf());
        if (b) rt->DrawEllipse(
            D2D1::Ellipse(D2D1::Point2F(micCX, micCY), micR, micR),
            b.Get(), 1.5f * s);
    }
    // Microphone symbol
    {
        const float mw = 8.0f * s;
        const float mh = 14.0f * s;
        const float mStand = 4.0f * s;
        const D2D1_COLOR_F symCol = m_vs.listening || m_vs.recognizing
            ? Tok::GoldBright(0.97f * ease)
            : Tok::ChromeMid(0.60f * ease);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
        rt->CreateSolidColorBrush(symCol, b.GetAddressOf());
        if (b) {
            D2D1_ROUNDED_RECT cap{ D2D1::RectF(
                micCX - mw*0.5f, micCY - mh*0.5f,
                micCX + mw*0.5f, micCY + mh*0.5f - 1.0f*s),
                mw*0.5f, mw*0.5f };
            rt->FillRoundedRectangle(cap, b.Get());
            // Stand line
            rt->DrawLine(
                D2D1::Point2F(micCX, micCY + mh*0.5f - 1.0f*s),
                D2D1::Point2F(micCX, micCY + mh*0.5f + mStand),
                b.Get(), 1.8f*s);
            // Chin arc
            rt->DrawLine(
                D2D1::Point2F(micCX - mw*0.6f, micCY + mh*0.5f + mStand),
                D2D1::Point2F(micCX + mw*0.6f, micCY + mh*0.5f + mStand),
                b.Get(), 1.8f*s);
        }
    }

    // Level percentage label below mic
    if (dwrite && (m_vs.listening || m_vs.recognizing)) {
        const int pct = static_cast<int>(std::round(m_levelSmooth * 100.0f));
        wchar_t pctBuf[8];
        _snwprintf_s(pctBuf, _countof(pctBuf), L"%d%%", pct);
        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
        dwrite->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 10.0f * s, L"en-us", fmt.GetAddressOf());
        if (fmt) {
            fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(
                m_levelSmooth > 0.6f ? Tok::GoldHi(0.90f * ease)
                : m_levelSmooth > 0.3f ? Tok::GoldMid(0.80f * ease)
                                       : Tok::ChromeMute(0.50f * ease),
                b.GetAddressOf());
            if (b) rt->DrawText(pctBuf, static_cast<UINT32>(std::wcslen(pctBuf)),
                fmt.Get(),
                D2D1::RectF(micCX - 24.0f*s, micCY + micR + 5.0f*s,
                            micCX + 24.0f*s, micCY + micR + 22.0f*s),
                b.Get());
        }
    }

    // Language badge
    if (dwrite && !m_langLabel.empty()) {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
        dwrite->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 9.0f * s, L"en-us", fmt.GetAddressOf());
        if (fmt) {
            const float bx = micCX + micR - 4.0f*s;
            const float by = micCY - micR - 1.0f*s;
            const float bw = 22.0f * s;
            const float bh = 13.0f * s;
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

    // =========================================================
    // RIGHT SECTION: Status text + VU bars + partial result
    // =========================================================
    const float rSectX  = px + 120.0f * s;
    const float rSectW  = pw - 120.0f * s - 20.0f * s;
    const float topY    = py + 18.0f * s;

    // ---- Status label
    if (dwrite) {
        const wchar_t* statusStr;
        if (m_vs.recognizing)   statusStr = L"\u25CF  Listening\u2026";
        else if (m_vs.listening) statusStr = L"\u25CB  Waiting for speech\u2026";
        else                     statusStr = L"\u25A1  Microphone inactive";

        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
        dwrite->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 14.0f * s, L"en-us", fmt.GetAddressOf());
        if (fmt) {
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            const D2D1_COLOR_F col = m_vs.recognizing
                ? Tok::GoldBright(0.98f * ease)
                : m_vs.listening
                    ? Tok::GoldMid(0.82f + 0.12f * std::sin(m_glowPhase) * ease)
                    : Tok::ChromeMute(0.55f * ease);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(col, b.GetAddressOf());
            if (b) rt->DrawText(statusStr, static_cast<UINT32>(std::wcslen(statusStr)),
                fmt.Get(),
                D2D1::RectF(rSectX, topY, rSectX + rSectW, topY + 24.0f*s),
                b.Get());
        }
    }

    // ---- 16-segment VU bar
    {
        const int   kBars    = 16;
        const float barAreaY = topY + 30.0f * s;
        const float barAreaH = 40.0f * s;
        const float barAreaW = rSectW;
        const float barGap   = 3.0f * s;
        const float totalGap = barGap * static_cast<float>(kBars - 1);
        const float barW     = (barAreaW - totalGap) / static_cast<float>(kBars);
        const float level    = std::clamp(m_levelBarSpring.value, 0.0f, 1.0f);
        const int   litBars  = static_cast<int>(level * static_cast<float>(kBars) + 0.5f);

        for (int i = 0; i < kBars; ++i) {
            const float bx   = rSectX + static_cast<float>(i) * (barW + barGap);
            const bool  lit  = (i < litBars);
            // Wave animation makes bars bob slightly when listening
            float waveOff = 0.0f;
            if (m_vs.listening || m_vs.recognizing) {
                const float wf = static_cast<float>(i) / static_cast<float>(kBars - 1);
                waveOff = std::sin(m_wavePhase + wf * kPif * 2.2f)
                          * level * barAreaH * 0.35f;
            }
            const float barFullH = barAreaH;
            const float barH     = lit
                ? (barAreaH * 0.35f + waveOff + level * barAreaH * 0.55f)
                : (barAreaH * 0.20f + waveOff * 0.3f);
            const float clampedH = std::clamp(barH, barAreaH * 0.08f, barAreaH);
            const float by       = barAreaY + (barFullH - clampedH);

            // Color gradient: gold (low) → amber (mid) → red-gold (high)
            float t = static_cast<float>(i) / static_cast<float>(kBars - 1);
            D2D1_COLOR_F col;
            if (!lit) {
                col = Tok::SurfaceSunken(0.55f * ease);
            } else if (t < 0.6f) {
                col = Tok::GoldMid((0.65f + t * 0.30f) * ease);
            } else if (t < 0.85f) {
                col = Tok::AmberWarm((0.70f + (t - 0.6f) * 0.40f) * ease);
            } else {
                col = D2D1::ColorF(0.92f, 0.38f, 0.18f, (0.80f + (t - 0.85f)) * ease);
            }

            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(col, b.GetAddressOf());
            if (b) {
                const float r2 = std::min(barW * 0.5f, 3.0f * s);
                D2D1_ROUNDED_RECT rr{D2D1::RectF(bx, by, bx + barW, barAreaY + barFullH), r2, r2};
                rt->FillRoundedRectangle(rr, b.Get());
            }
        }
    }

    // ---- Partial result text
    if (dwrite) {
        const float partY = topY + 80.0f * s;
        const float partH = 26.0f * s;
        rt->PushAxisAlignedClip(
            D2D1::RectF(rSectX, partY, rSectX + rSectW, partY + partH),
            D2D1_ANTIALIAS_MODE_ALIASED);
        if (!m_vs.partial.empty()) {
            Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtP;
            dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_ITALIC,
                DWRITE_FONT_STRETCH_NORMAL, 13.0f * s, L"ru-ru", fmtP.GetAddressOf());
            if (fmtP) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(Tok::GoldHi(0.90f * ease), b.GetAddressOf());
                if (b) rt->DrawText(
                    m_vs.partial.c_str(), static_cast<UINT32>(m_vs.partial.size()),
                    fmtP.Get(),
                    D2D1::RectF(rSectX - m_partialScrollX, partY,
                                rSectX + rSectW * 3.0f, partY + partH),
                    b.Get());
            }
        } else {
            // Idle placeholder
            static const wchar_t kPlaceholder[] = L"Say something\u2026";
            const float pAlpha = m_vs.listening
                ? 0.30f + 0.15f * std::sin(m_glowPhase)
                : 0.15f;
            Microsoft::WRL::ComPtr<IDWriteTextFormat> fmtP;
            dwrite->CreateTextFormat(L"Segoe UI", nullptr,
                DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_ITALIC,
                DWRITE_FONT_STRETCH_NORMAL, 13.0f * s, L"ru-ru", fmtP.GetAddressOf());
            if (fmtP) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
                rt->CreateSolidColorBrush(Tok::ChromeMute(pAlpha * ease), b.GetAddressOf());
                if (b) rt->DrawText(
                    kPlaceholder, static_cast<UINT32>(std::wcslen(kPlaceholder)),
                    fmtP.Get(),
                    D2D1::RectF(rSectX, partY, rSectX + rSectW, partY + partH),
                    b.Get());
            }
        }
        rt->PopAxisAlignedClip();
    }

    // ---- Hint chips row (LB+RB exit)
    if (dwrite) {
        const float hintY = py + ph - 30.0f * s;
        static const wchar_t kHint[] = L"LB + RB  \u2014  exit voice mode    \u2665  switch language";
        Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
        dwrite->CreateTextFormat(L"Segoe UI", nullptr,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, 10.0f * s, L"en-us", fmt.GetAddressOf());
        if (fmt) {
            fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;
            rt->CreateSolidColorBrush(Tok::ChromeMute(0.42f * ease), b.GetAddressOf());
            if (b) rt->DrawText(kHint, static_cast<UINT32>(std::wcslen(kHint)),
                fmt.Get(),
                D2D1::RectF(px + 12.0f*s, hintY, px + pw - 12.0f*s, hintY + 20.0f*s),
                b.Get());
        }
    }

    rt->SetTransform(oldTransform);
}

} // namespace enjoystick::overlay
