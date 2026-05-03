# 🎮 Enjoystick Windows

> **Seamless. Stylish. Controller-native.**  
> A polished UX/UI extension that transforms Windows 10/11 into a fully
> gamepad-navigable environment — from gaming to deep productivity.

[![Build](https://github.com/nkVas1/enjoystick-windows/actions/workflows/build.yml/badge.svg)](https://github.com/nkVas1/enjoystick-windows/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](./LICENSE)

---

## ✨ What It Does

EnjoyStick Windows is a **non-intrusive system overlay** that augments Windows
with a controller-first experience — designed with the elegance of Steam Deck’s
Big Picture Mode, the fluency of PlayStation’s system UI, and the power of
Xbox’s Game Bar, then taken further.

Install it once. It works *out of the box*. No config files. No friction.

---

## 🎯 Key Features

| Feature | Description |
|---|---|
| **Virtual Cursor** | Right-stick → smooth power-curve mouse; triggers → clicks |
| **Keyboard Nav** | D-pad / buttons → keyboard events for any Windows app |
| **Radial Quick Menu** | Guide button → instant radial overlay (Direct2D, 60 fps) |
| **System Tray** | Right-click tray icon for mode toggle, settings, auto-start |
| **Hot-reload Config** | Edit `%APPDATA%\Enjoystick\config.json`, changes apply live |
| **Auto-start** | Registers in `HKCU\...\Run` on first launch (no elevation) |
| **Toast Notifications** | Controller connected/disconnected, mode changes |
| **Multi-controller** | Up to 4 simultaneous XInput controllers |

---

## 🛠️ Tech Stack

| Layer | Technology |
|---|---|
| Input engine | C++20, XInput 1.4 |
| Cursor / keyboard | Win32 `SendInput` |
| Overlay rendering | Direct2D, DirectWrite, DirectComposition |
| System tray | `Shell_NotifyIcon` (Win32) |
| Config | Hand-rolled JSON, `ReadDirectoryChangesW` hot-reload |
| Build system | CMake 3.20+ · MSVC (VS 2019 / VS 2022) · Ninja or MSBuild |
| CI | GitHub Actions · MSVC x64 Debug + Release matrix |

---

## 🚀 Quick Start

### Prerequisites

| Tool | Minimum version | Notes |
|---|---|---|
| Windows | 10 21H2 or 11 | Target OS |
| Visual Studio / Build Tools | **2019** (16.x) or 2022 | Include *Desktop C++* workload |
| CMake | **3.20** | Bundled with VS; or install separately |
| Git | any | |
| vcpkg *(optional)* | latest | Only needed for future third-party deps |

> ⚠️ **VS 2019 users:** Use the `vs2019-debug` / `vs2019-release` CMake presets
> (see below). The default `windows-release` preset uses Ninja which may not
> be in your PATH.

### 1. Clone

```powershell
git clone https://github.com/nkVas1/enjoystick-windows.git
cd enjoystick-windows
```

### 2. Configure

**Option A — VS 2019 Build Tools (recommended for your setup)**
```powershell
# Open a "x64 Native Tools Command Prompt for VS 2019", then:
cmake -B build/vs2019-release -G "Visual Studio 16 2019" -A x64 -DCMAKE_BUILD_TYPE=Release
```

**Option B — VS Code CMake Tools with preset**  
Select the preset **"VS 2019 x64 Release (no Ninja required)"** from the
CMake Tools preset picker in VS Code.

**Option C — Ninja + VS 2019 (if Ninja is installed)**
```powershell
# Set VCPKG_ROOT if you use vcpkg, otherwise skip
$env:VCPKG_ROOT = "C:\vcpkg"   # optional

cmake --preset windows-release
```

### 3. Build

```powershell
# MSBuild (Option A / B)
cmake --build build/vs2019-release --config Release --parallel

# Ninja (Option C)
cmake --build build/windows-release
```

The executable lands at `build/<preset>/Release/EnjoyStick.exe`.

### 4. Run

```powershell
.\build\vs2019-release\Release\EnjoyStick.exe
```

A tray icon appears. Connect any Xbox or PlayStation controller and go.

---

## ⚙️ Configuration

Settings are stored in `%APPDATA%\Enjoystick\config.json` (created automatically
on first run). Edit with any text editor — changes apply **live** without
restarting.

```jsonc
{
  "cursor_maxSpeedPx":       2000.0,   // max cursor speed px/s
  "cursor_curveExponent":    0.55,     // 0.3 = very smooth, 1.0 = linear
  "cursor_accelerationMs":   80.0,     // ramp-up time in ms
  "cursor_useRightStick":    true,     // false = left stick
  "cursor_triggersAsClicks": true,     // LT/RT -> left/right click
  "cursor_scrollSpeed":      8.0,      // scroll lines/s at full D-pad
  "cursor_invertScroll":     false,
  "dz_innerRadius":          0.08,     // inner deadzone [0, 0.5]
  "dz_outerRadius":          0.98,     // outer edge clamp
  "dz_mode":                 3         // 0=None 1=Axial 2=Radial 3=ScaledRadial
}
```

---

## 🎮 Default Controller Bindings

| Button | Action |
|---|---|
| Right stick | Mouse cursor |
| Left trigger | Left click |
| Right trigger | Right click |
| D-pad Up / Down | Scroll |
| D-pad arrows (Navigate mode) | Arrow keys |
| **A / Cross** | Enter |
| **B / Circle** | Escape |
| **Y / Triangle** | F5 |
| **X / Square** | Space |
| LB | Tab |
| RB | Shift + Tab |
| Select / Share | Win key |
| RS click | Context menu |
| LS click | F2 (rename) |
| **Guide / PS button** | Open radial menu |

---

## 📁 Project Structure

```
enjystick-windows/
├── src/
│   ├── core/       InputEngine, XInputBackend, DeadzoneFilter, HapticsEngine
│   ├── cursor/     VirtualMouse (SendInput, power curve, sub-pixel)
│   ├── input/      KeyboardMapper (button → key bindings)
│   ├── overlay/    OverlayWindow (D2D), RadialMenu, Toast notifications
│   ├── config/     ConfigStore (JSON + hot-reload)
│   ├── app/        Application, SystemTray, AutoStart
│   └── main.cpp    WinMain entry point
├── .github/workflows/  CI build + lint
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
└── CHANGELOG.md
```

---

## 🧑‍💻 Contributing

See [CONTRIBUTING.md](./CONTRIBUTING.md). Code quality bar is senior-level.
All PRs require tests for new logic, design review for UI changes, and
performance profiling for input-path changes.

---

## 📜 License

MIT — see [LICENSE](./LICENSE)

---

<p align="center"><sub>Built with obsessive attention to craft. 🎮</sub></p>
