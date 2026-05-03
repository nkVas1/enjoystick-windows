# 🎮 Enjoystick Windows

> **Seamless. Stylish. Controller-native.**  
> A polished UX/UI extension that transforms Windows 11 into a fully gamepad-navigable environment — from gaming to deep productivity.

---

## ✨ Vision

Enjostick Windows is a non-intrusive system overlay that augments Windows 11 with a **controller-first experience** — designed with the elegance of Steam Deck's Big Picture Mode, the fluency of PlayStation's system UI, and the power of Xbox's Game Bar, then taken further.

Install it once. It works *out of the box*. No config files. No friction.

---

## 🏗️ Architecture Overview

```
Enjoystick Windows
├── Core (C++ / WinRT)          – low-level input pipeline, process injection
├── Shell (WinUI 3 / XAML)      – overlay UI layer, animations, design system
├── InputRouter                 – gamepad → system events (mouse, keyboard, nav)
├── AppAdapter                  – per-app gamepad profile engine
├── MediaEngine                 – media-mode (video/audio focused layout)
├── BrowserBridge               – controller-aware web navigation layer
├── Daemon (background service) – always-on input listener, low-latency
└── Installer / OOB setup       – silent, automated, zero-friction install
```

---

## 🎯 Key Features

| Feature | Description |
|---|---|
| **Universal Navigation** | Navigate any Windows UI — Explorer, Settings, apps — with a gamepad |
| **Radial Quick Menu** | Hold a button → instant radial overlay for system actions |
| **Context-Aware Layouts** | Auto-detects mode: Gaming / Media / Browser / Desktop / Productivity |
| **Per-App Profiles** | Custom bindings per application, synced to cloud |
| **Virtual Cursor** | Smooth, physics-based analog cursor for non-gamepad-native apps |
| **On-Screen Keyboard** | Fluid, fast input keyboard optimized for controller |
| **Media Cinematic Mode** | Full-screen video/music mode with controller media controls |
| **Browser Mode** | Spatial navigation + URL bar + tab management via stick |
| **System Widgets** | Volume, brightness, notifications, power — all accessible via controller |
| **Zero-Friction Install** | One-click installer; auto-starts with Windows; no driver conflicts |

---

## 🛠️ Tech Stack

| Layer | Technology |
|---|---|
| Core Input Engine | C++20, WinRT, XInput / SDL3, RawInput |
| UI Overlay | WinUI 3 (Windows App SDK), XAML Islands |
| Animations | Lottie for WinUI, Composition API |
| Background Daemon | C++ Windows Service (low-latency loop) |
| App logic / glue | C# (.NET 8) |
| Build System | CMake + MSBuild, GitHub Actions CI |
| Installer | WiX Toolset v4 + custom bootstrapper |
| Design Tokens | Fluent Design System extended |

---

## 🚀 Getting Started

### Prerequisites
- Windows 11 22H2+ (also tested on Windows 10 21H2+)
- Xbox or PlayStation controller (USB / Bluetooth / Wireless)
- .NET 8 Runtime (bundled in installer)
- Visual C++ Redistributable 2022 (bundled)

### Install (release)
```powershell
# Download latest release and run:
.\EnjostickSetup.exe
# That's it. The system tray icon will appear.
```

### Build from Source
```powershell
git clone https://github.com/nkVas1/enjoystick-windows.git
cd enjoystick-windows
.\scripts\bootstrap.ps1        # installs dependencies
cmake --preset windows-release
cmake --build build --config Release
```

---

## 📐 Design Philosophy

- **Controller-first, not mouse-last** — every interaction is designed for analog + digital input, not retrofitted.
- **Invisible when idle** — the system disappears when not needed, never interrupting workflow.
- **Pixel-perfect motion** — 60fps+ animations, spring physics, no jarring transitions.
- **ArtStation-level craft** — every icon, blur, gradient and easing curve is intentional.
- **Indy soul, studio polish** — the warmth of a passion project, the quality of a AAA product.

---

## 📁 Repository Structure

```
/src
  /core          C++ input engine & daemon
  /shell         WinUI 3 overlay application
  /inputrouter   Gamepad-to-system input mapping
  /appadapter    Per-app profile engine
  /mediaengine   Cinematic media mode
  /browserbridge Controller-native browser layer
  /shared        Shared types, utilities, design tokens
/installer       WiX installer project
/scripts         Build, bootstrap, CI helpers
/docs            Architecture diagrams, UX spec, design tokens
/tests           Unit + integration + UI tests
/.github         CI/CD workflows, issue templates
```

---

## 🤝 Contributing

See [CONTRIBUTING.md](./CONTRIBUTING.md). Code quality bar is senior-level. All PRs require:
- Tests for new logic
- Design review for UI changes
- Performance profiling for input-path changes

---

## 📜 License

MIT — see [LICENSE](./LICENSE)

---

<p align="center"><sub>Built with obsessive attention to craft. 🎮</sub></p>
