#pragma once

// ---------------------------------------------------------------------------
// Design tokens — Dark Regalia visual language
//
// Inspired by: baroque dark-gold heraldic 3-D renders.
// Key palette:
//   Obsidian base    #0A0A0E  rgb(10,10,14)
//   Deep void        #050508  rgb(5,5,8)
//   Gold warm hi     #E8C97A  rgb(232,201,122)
//   Gold mid         #C9A84C  rgb(201,168,76)
//   Gold shadow      #8A6A1F  rgb(138,106,31)
//   Silver light     #D0D0DC  rgb(208,208,220)
//   Silver mid       #B8B8C8  rgb(184,184,200)
//   Silver muted     #888898  rgb(136,136,152)
//   Void black       #01010  alpha-only fills
//
// Usage: include this header and call Tok::GoldMid() etc.
// ---------------------------------------------------------------------------

#include <d2d1.h>

namespace enjoystick::overlay {

struct Tok {
    // ---- base fills --------------------------------------------------------
    static D2D1_COLOR_F ObsidianBase(float a = 0.92f) noexcept {
        return D2D1::ColorF(0.040f, 0.039f, 0.055f, a); }
    static D2D1_COLOR_F DeepVoid(float a = 0.96f) noexcept {
        return D2D1::ColorF(0.020f, 0.018f, 0.032f, a); }

    // ---- gold family -------------------------------------------------------
    static D2D1_COLOR_F GoldHi(float a = 1.0f) noexcept {
        return D2D1::ColorF(0.910f, 0.788f, 0.478f, a); }  // #E8C97A
    static D2D1_COLOR_F GoldMid(float a = 1.0f) noexcept {
        return D2D1::ColorF(0.788f, 0.659f, 0.298f, a); }  // #C9A84C
    static D2D1_COLOR_F GoldShadow(float a = 1.0f) noexcept {
        return D2D1::ColorF(0.541f, 0.416f, 0.122f, a); }  // #8A6A1F
    static D2D1_COLOR_F GoldGlow(float a = 0.18f) noexcept {
        return D2D1::ColorF(0.910f, 0.788f, 0.478f, a); }  // wide outer halo

    // ---- silver family -----------------------------------------------------
    static D2D1_COLOR_F SilverLt(float a = 1.0f) noexcept {
        return D2D1::ColorF(0.816f, 0.816f, 0.863f, a); }  // #D0D0DC
    static D2D1_COLOR_F SilverMid(float a = 1.0f) noexcept {
        return D2D1::ColorF(0.722f, 0.722f, 0.784f, a); }  // #B8B8C8
    static D2D1_COLOR_F SilverMute(float a = 1.0f) noexcept {
        return D2D1::ColorF(0.533f, 0.533f, 0.596f, a); }  // #888898

    // ---- utility -----------------------------------------------------------
    static D2D1_COLOR_F White(float a = 1.0f) noexcept {
        return D2D1::ColorF(1.0f, 1.0f, 1.0f, a); }
    static D2D1_COLOR_F Transparent() noexcept {
        return D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f); }
};

} // namespace enjoystick::overlay
