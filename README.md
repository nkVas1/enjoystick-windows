# EnjoyStick Windows

> Seamless gamepad navigation for Windows 10/11 — cursor, keyboard, overlay and radial menu, all in one.

[![Build](https://img.shields.io/badge/build-passing-brightgreen)](#building)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11-blue)](#requirements)

---

## What it does

| Feature | Status | Notes |
|---|---|---|
| Right-stick → mouse cursor | ✅ Working | Acceleration curve, sub-pixel precision |
| Triggers → left/right click | ✅ Working | Threshold 50%, configurable |
| Trigger scroll (no-click mode) | ✅ Working | LT=up, RT=down |
| D-pad / buttons → keyboard | ✅ Working | Navigate mode |
| Transparent overlay (HUD) | ✅ Working | Per-pixel alpha, Direct2D |
| Active indicator dot | ✅ Working | Bottom-right corner |
| Toast notifications | ✅ Working | Controller connect/disconnect, mode change |
| Radial quick-action menu | ✅ Working | Guide button, 7 items, animated |
| Radial menu item icons + text | ✅ Working | Segoe UI Emoji via DWrite |
| System tray icon + menu | ✅ Working | Right-click for options |
| Auto-start on login | ✅ Working | HKCU Run key, silent |
| Config JSON + hot-reload | ✅ Working | %APPDATA%\\Enjoystick\\config.json |
| Haptic feedback | ✅ Working | Mode switch double-pulse |
| LB+RB chord mode toggle | ✅ Working | Instant, no menu needed |
| Select (Back) mode toggle | ✅ Working | Single-press shortcut |
| NSIS installer | 🔧 Planned | Phase 2 |
| Settings UI | 🔧 Planned | Phase 2 |
| On-screen keyboard (OSK) | 🔧 Planned | Phase 2 |
| Multi-monitor support | 🔧 Planned | Phase 2 |

---

## Requirements

- Windows 10 20H2+ or Windows 11
- Visual Studio 2019 or 2022 with **Desktop development with C++** workload
  (Build Tools edition works too)
- CMake 3.21+ (bundled with VS, no separate install needed)
- Xbox or PlayStation controller (XInput-compatible)

---

## Building

### Quick build (recommended)

Double-click **`EnjoyStick.bat`** in the repo root, press **Y** when prompted.

Or run from PowerShell:

```powershell
.\scripts\build.ps1
```

Output: `build\Release\Release\EnjoyStick.exe`

### Rebuild after pulling changes

The fastest way to rebuild after a `git pull`:

```powershell
# Pull latest changes
git pull

# Rebuild (incremental — only changed files recompile)
.\scripts\build.ps1
```

CMake's incremental build will only recompile changed `.cpp` files. A full
rebuild from scratch usually takes ~30 seconds; incremental rebuilds take 3–8s.

### Build options

```powershell
# Release build (default)
.\scripts\build.ps1

# Debug build (symbols, no optimisation)
.\scripts\build.ps1 -Config Debug

# Build AND immediately launch
.\scripts\build.ps1 -Run

# Wipe build directory and rebuild from scratch
.\scripts\build.ps1 -Clean

# Combined: clean Debug build + run
.\scripts\build.ps1 -Config Debug -Clean -Run
```

### Manual CMake (advanced)

```powershell
cmake -S . -B build\Release -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build\Release --config Release --parallel
```

---

## Controls

### Cursor mode (default)

| Input | Action |
|---|---|
| Right stick | Mouse cursor |
| Right trigger (RT) | Left click |
| Left trigger (LT) | Right click |
| Guide button | Open radial quick-action menu |
| LB + RB (held) | Switch to Navigate mode |
| Select / Back | Switch to Navigate mode |

### Navigate mode

| Input | Action |
|---|---|
| D-pad | Arrow keys |
| South (A/Cross) | Enter |
| East (B/Circle) | Escape |
| North (Y/Triangle) | F5 |
| West (X/Square) | Space |
| LB | Tab |
| RB | Shift+Tab |
| Start | Escape |
| Select / Back | Switch to Cursor mode |
| LB + RB (held) | Switch to Cursor mode |

### Radial menu (Guide button)

| Stick direction | Action |
|---|---|
| Up | Desktop |
| Right | Files (Explorer) |
| Down-right | Settings |
| Down | Search (Win+S) |
| Down-left | Media Play/Pause |
| Left | Toggle Mode |
| Up-left | Exit EnjoyStick |

Confirm selection: **South (A/Cross)**  
Cancel / close menu: **East (B/Circle)**

### Power-user shortcuts

| Input | Action |
|---|---|
| Guide + LT | Force Cursor mode |
| Guide + RT | Force Navigate mode |

---

## Config file

Location: `%APPDATA%\Enjoystick\config.json`

The file is created automatically on first launch with sensible defaults.
Edit and save — changes apply instantly (hot-reload via ReadDirectoryChangesW).

```json
{
  "cursor": {
    "maxSpeedPx": 1800.0,
    "curveExponent": 1.8,
    "accelerationMs": 120.0,
    "triggersAsClicks": true,
    "scrollSpeed": 8.0,
    "invertScroll": false
  },
  "deadzone": {
    "type": "ScaledRadial",
    "inner": 0.12,
    "outer": 0.98
  }
}
```

---

## Project structure

```
enjoystick-windows/
├── src/
│   ├── app/          Application entry, SystemTray, AutoStart
│   ├── config/       ConfigStore (JSON + hot-reload)
│   ├── core/         InputEngine, XInput backend, DeadzoneFilter
│   ├── cursor/       VirtualMouse (SendInput)
│   ├── input/        KeyboardMapper (SendInput)
│   └── overlay/      OverlayWindow (Direct2D), RadialMenu
├── scripts/
│   ├── build.ps1     Main build script
│   ├── run.ps1       Launch existing build
│   └── clean.ps1     Wipe build directory
├── EnjoyStick.bat  Double-click launcher (prompts to build if needed)
└── CMakeLists.txt  Root CMake configuration
```

---

## Architecture highlights

- **Lock-free state pipeline**: XInput polling thread → atomic double-buffer → render thread. Zero mutex contention on the hot path.
- **Per-pixel alpha overlay**: `UpdateLayeredWindow(ULW_ALPHA)` + DIB surface. Genuine transparency — only drawn pixels are visible.
- **Sub-pixel mouse precision**: Accumulator pattern avoids integer truncation at slow stick speeds.
- **Configurable deadzone**: ScaledRadial mode scales the usable range to [0,1] after the inner deadzone, eliminating the dead band at slow movement.
- **Hot-reload config**: `ReadDirectoryChangesW` watcher; changes propagate to subsystems on the watcher thread without restart.

---

## Roadmap

### Phase 2
- NSIS installer with Start Menu shortcut and uninstall entry
- Settings overlay (accessible from radial menu) for live config editing
- On-screen radial keyboard for text entry
- Multi-monitor: overlay follows active window’s monitor
- Steam / full-screen game detection: auto-suspend when a full-screen DX app is active

### Phase 3
- Profile system: per-app control schemes
- HID/RawInput backend for non-XInput controllers (PS4/PS5 via USB)
- Macro recorder: record button sequences and replay them
- Community profile sharing
