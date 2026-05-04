#pragma once

// =============================================================================
// EnjoyStick Design System  —  "Futurist Glamour"  v3
//
// Philosophy:
//   Minimal surfaces, maximal depth.
//   Every element is sculpted — bevel rim, specular arc, shadow fill.
//   Gold is structural, never decorative.
//   Motion is physical: cubic ease-out open, cubic ease-in close.
//   Inspired by: Apple HIG, Braun industrial, dark-matter luxury product UI.
//
// Palette:
//   Surface tier:
//     SurfaceSunken  #060609  rgb(  6,  6,  9)  deep well / input bg
//     SurfaceBase    #0C0C12  rgb( 12, 12, 18)  main panel fill
//     SurfaceRaised  #14141E  rgb( 20, 20, 30)  cards / floating
//     SurfaceGlass   #0E0E18  rgb( 14, 14, 24)  frosted modal bg (+ alpha)
//     InkLine        #1E1E2A  rgb( 30, 30, 42)  hairline borders
//
//   Gold tier (7 steps, dark → bright):
//     GoldDeep       #6B4F14  rgb(107, 79, 20)  shadow / pressed
//     GoldShadow     #8A6A1F  rgb(138,106, 31)  ambient / unlit side
//     GoldMid        #C9A84C  rgb(201,168, 76)  mid-tone interactive
//     GoldWarm       #D4A84B  rgb(212,168, 75)  warm active fill
//     GoldHi         #E8C97A  rgb(232,201,122)  primary bright gold
//     GoldAccent     #F0D080  rgb(240,208,128)  specular top highlight
//     GoldBright     #F5DFA0  rgb(245,223,160)  maximum specular / key top
//     GoldGlow       #E8C97A + alpha             wide diffuse halo
//
//   Amber:
//     AmberWarm      #E8A020  rgb(232,160, 32)  active toggle glow
//
//   Chrome tier:
//     ChromeHi       #E8E8F0  rgb(232,232,240)  primary text on dark
//     ChromeMid      #A0A0B4  rgb(160,160,180)  secondary text
//     ChromeMute     #5A5A72  rgb( 90, 90,114)  disabled / hint
//
//   Utility:
//     White          #FFFFFF                    top specular gleam
//     Scrim          #050408 + alpha            modal background dim
// =============================================================================

#include <d2d1.h>

namespace enjoystick::overlay {

struct Tok {
    // =========================================================================
    // Surface tier
    // =========================================================================
    static D2D1_COLOR_F SurfaceSunken (float a = 1.0f) noexcept {
        return D2D1::ColorF(0.024f, 0.024f, 0.035f, a); }
    static D2D1_COLOR_F SurfaceBase   (float a = 1.0f) noexcept {
        return D2D1::ColorF(0.047f, 0.047f, 0.071f, a); }
    static D2D1_COLOR_F SurfaceRaised (float a = 1.0f) noexcept {
        return D2D1::ColorF(0.078f, 0.078f, 0.118f, a); }
    static D2D1_COLOR_F SurfaceGlass  (float a = 0.88f) noexcept {
        return D2D1::ColorF(0.055f, 0.055f, 0.094f, a); }
    static D2D1_COLOR_F InkLine       (float a = 1.0f) noexcept {
        return D2D1::ColorF(0.118f, 0.118f, 0.165f, a); }

    // =========================================================================
    // Gold tier
    // =========================================================================
    static D2D1_COLOR_F GoldBright (float a = 1.0f) noexcept {
        return D2D1::ColorF(0.961f, 0.875f, 0.627f, a); } // #F5DFA0
    static D2D1_COLOR_F GoldAccent (float a = 1.0f) noexcept {
        return D2D1::ColorF(0.941f, 0.816f, 0.502f, a); } // #F0D080
    static D2D1_COLOR_F GoldHi     (float a = 1.0f) noexcept {
        return D2D1::ColorF(0.910f, 0.788f, 0.478f, a); } // #E8C97A
    static D2D1_COLOR_F GoldWarm   (float a = 1.0f) noexcept {
        return D2D1::ColorF(0.831f, 0.659f, 0.294f, a); } // #D4A84B
    static D2D1_COLOR_F GoldMid    (float a = 1.0f) noexcept {
        return D2D1::ColorF(0.788f, 0.659f, 0.298f, a); } // #C9A84C
    static D2D1_COLOR_F GoldShadow (float a = 1.0f) noexcept {
        return D2D1::ColorF(0.541f, 0.416f, 0.122f, a); } // #8A6A1F
    static D2D1_COLOR_F GoldDeep   (float a = 1.0f) noexcept {
        return D2D1::ColorF(0.420f, 0.310f, 0.078f, a); } // #6B4F14
    static D2D1_COLOR_F GoldGlow   (float a = 0.18f) noexcept {
        return D2D1::ColorF(0.910f, 0.788f, 0.478f, a); }

    // =========================================================================
    // Amber
    // =========================================================================
    static D2D1_COLOR_F AmberWarm  (float a = 1.0f) noexcept {
        return D2D1::ColorF(0.910f, 0.627f, 0.125f, a); } // #E8A020

    // =========================================================================
    // Chrome tier
    // =========================================================================
    static D2D1_COLOR_F ChromeHi   (float a = 1.0f) noexcept {
        return D2D1::ColorF(0.910f, 0.910f, 0.941f, a); } // #E8E8F0
    static D2D1_COLOR_F ChromeMid  (float a = 1.0f) noexcept {
        return D2D1::ColorF(0.627f, 0.627f, 0.706f, a); } // #A0A0B4
    static D2D1_COLOR_F ChromeMute (float a = 1.0f) noexcept {
        return D2D1::ColorF(0.353f, 0.353f, 0.447f, a); } // #5A5A72

    // =========================================================================
    // Utility
    // =========================================================================
    static D2D1_COLOR_F White      (float a = 1.0f) noexcept {
        return D2D1::ColorF(1.0f, 1.0f, 1.0f, a); }
    static D2D1_COLOR_F Transparent() noexcept {
        return D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f); }
    static D2D1_COLOR_F Scrim      (float a = 0.60f) noexcept {
        return D2D1::ColorF(0.020f, 0.016f, 0.032f, a); }

    // =========================================================================
    // Legacy aliases  (kept for build compatibility, do not use in new code)
    // =========================================================================
    static D2D1_COLOR_F ObsidianBase(float a = 0.92f) noexcept { return SurfaceBase(a); }
    static D2D1_COLOR_F DeepVoid    (float a = 0.96f) noexcept { return SurfaceSunken(a); }
    static D2D1_COLOR_F SilverLt    (float a = 1.0f)  noexcept { return ChromeHi(a); }
    static D2D1_COLOR_F SilverMid   (float a = 1.0f)  noexcept { return ChromeMid(a); }
    static D2D1_COLOR_F SilverMute  (float a = 1.0f)  noexcept { return ChromeMute(a); }
};

} // namespace enjoystick::overlay
