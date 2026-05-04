# Changelog

All notable changes to Enjoystick Windows will be documented here.

Format follows [Keep a Changelog](https://keepachangelog.com/) and [Semantic Versioning](https://semver.org/).

## [Unreleased] — Phase 2 in progress

### Added

#### Core Engine
- `InputBackend_HID` — DualSense (Sony VID 0x054C, PID 0x0CE6/0x0DF2) HID backend
  - Enumerates HID devices via `SetupDiGetClassDevs`; matches Sony VID+PID from path string
  - Opens device with `GENERIC_READ|GENERIC_WRITE` (falls back to read-only if Access Denied)
  - Overlapped `ReadFile` on a dedicated high-priority thread with 100 ms WaitForSingleObject
  - Parses USB Input Report 0x01 (65 bytes): both sticks, both triggers, all 16 buttons, d-pad
  - D-pad decoded from nibble (8 directions → 4 cardinal `Button` flags)
  - Touchpad button mapped to `Button::Touchpad`
  - Rumble: Output Report 0x02 (64 bytes, flags byte 0xFF, left/right motors at bytes 3–4)
  - Bluetooth detection via `HIDP_CAPS.InputReportByteLength >= 78` (graceful skip for now)
  - `GetConnectedControllers()` reports `ControllerType::PlayStation`
- `InputEngine::Create()` — backend auto-selection strategy
  - `Auto` (default): try `HidBackend::TryOpen()` first; fall back to `XInputBackend`
  - `XInputOnly`: always use XInputBackend
  - `HIDOnly`: use HidBackend; throw `std::runtime_error` if no DualSense found
- `core/CMakeLists.txt`: added `InputBackend_HID.cpp`; linked `hid` and `setupapi` system libs

#### Tests
- `tests/test_config.cpp` — GoogleTest suite for config subsystem
  - `ConfigSerializer_RoundTrip`: full Config → JSON → Config round-trip including all three
    new MouseCfg fields (`triggersAsClicks`, `useRightStick`, `accelerationMs`)
  - `ConfigSerializer_DefaultsOnMissingFields`: empty JSON object produces `Config::Defaults()`
  - `ConfigSerializer_BadJsonThrows`: invalid/empty/truncated JSON throws `std::runtime_error`
  - `ConfigSerializer_KeyMappingRoundTrip`: `KeyMappingEntry` with VKey sequence survives round-trip
  - `SettingsValues_AllFieldsReadFromConfig`: regression guard for the hardcoded-fields bug
  - `SettingsValues_DefaultUseRightStickIsTrue`: sanity check on default config value
- `tests/CMakeLists.txt`: added `test_config.cpp` to sources; linked `enjoystick_config`

### Fixed

#### App
- `Application.cpp` — `SettingsValuesFromConfig()`: `triggersAsClicks`, `useRightStick`,
  `accelerationMs` were hardcoded to `false`, `true`, `0.0f`; now read from `cfg.mouse`
- `Application.cpp` — config-watcher hot-reload lambda: same three fields were missing from
  the `cursor::MouseConfig` update passed to `VirtualMouse::SetConfig()`; now propagated correctly
- `Application.cpp` — `Init()` initial `vmCfg` construction: also propagates all three new
  fields on first boot, consistent with hot-reload path

---

## [Unreleased] — Phase 1 Core Systems

### Added

#### Core Engine
- `shared/Types.hpp` — canonical types: `Vec2`, `Rect`, `ControllerState`, `Button` bitmask,
  `RumbleParams`, `CallbackHandle` (RAII unregister), `InputBackendPreference`
- `DeadzoneFilter` — three modes: Axial, Radial, ScaledRadial (PlayStation-style default)
- `InputEngine` — abstract polling interface, 250 Hz default, haptics, multi-controller
- `XInputBackend` — XInput 1.4 poller with packet-number dedup and rising/falling edge tracking
- `HapticsEngine` — timed rumble dispatch on dedicated thread

#### Cursor Module (`src/cursor/`)
- `VirtualMouse` — right-stick → SendInput cursor movement
  - Power-curve velocity (`pow(mag, 0.55)`) for precision at low deflection
  - Sub-pixel accumulator for perfectly smooth motion at all speeds
  - Exponential velocity smoothing with configurable ramp-up time
  - `leftTrigger` → left-click, `rightTrigger` → right-click (half-press threshold)
  - D-pad Up/Down → scroll wheel with line accumulator
  - Graceful destructor: releases any held buttons on shutdown

#### Input Module (`src/input/`)
- `KeyboardMapper` — button-to-key binding engine
  - Default Steam-Deck-inspired navigation bindings out-of-the-box
  - D-pad → Arrow keys, South → Enter, East → Esc, West → Space
  - LB → Tab, RB → Shift+Tab (reverse focus), Select → Win key
  - RS-click → Application/context-menu key, LS-click → F2 (rename)
  - Full runtime rebinding API with `Bind()` / `Unbind()` / `ResetToDefaults()`
  - Modifier key injection (Shift, Ctrl, Alt) with correct down/up ordering

#### Overlay Module (`src/overlay/`)
- `RadialMenu` — controller-native circular menu
  - Items arranged in a circle, selection driven by right-stick angle (atan2)
  - SmoothStep scale animation (140 ms, configurable)
  - South (A/Cross) → confirm action, East (B/Circle) → cancel
  - Direct2D rendering: semi-transparent disc, per-item ellipses, violet hover accent
  - Default quick-action ring: Desktop, Files, Settings, Search, Media, Toggle Mode, Exit
- `OverlayWindow` — DirectComposition layered HWND
  - `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE`
  - Lock-free double-buffer state handoff between input thread and render thread
  - 60 Hz render loop with frame-time cap
  - Toast notification queue (slide-in / fade-out, Phase 2 full rendering)
  - Multi-monitor aware (targets specific `HMONITOR` by index)

#### Config Module (`src/config/`)
- `ConfigStore` — JSON settings persistence
  - Path: `%APPDATA%\Enjoystick\config.json` (auto-created on first run)
  - Zero external dependencies: hand-rolled flat JSON serialiser/parser
  - Value clamping on load ensures no corrupt config can crash the app
  - Hot-reload via `ReadDirectoryChangesW` with 150 ms debounce
  - `CallbackHandle`-based subscription for live config propagation

#### App Module (`src/app/`)
- `Application` — composition root that owns and wires all subsystems
  - `InputMode::Cursor` / `InputMode::Navigate` toggle (Guide button)
  - Guide button opens/closes RadialMenu
  - Welcome rumble on controller connect
  - Single-instance mutex guard
- `AutoStart` — `HKCU\...\Run` registry helper (no elevation required)
  - Silently registers on first launch
  - `Enable()` / `Disable()` / `IsEnabled()` API
- `SystemTray` — `Shell_NotifyIcon` wrapper with context menu (implementation Phase 2)
- `main.cpp` — `wWinMain` entry point with single-instance guard and fatal-error dialog

#### Build & CI
- Root `CMakeLists.txt` updated: all 6 modules wired, MSVC strict flags, Release LTCG
- `.github/workflows/build.yml` — MSVC x64 Debug + Release matrix, artifact upload
- `.github/workflows/lint.yml` — clang-format-17 diff check
- `.clang-format` — Microsoft style base, 100-col limit, include grouping

## [0.0.1] — 2026-05-03 Initial scaffold

### Added
- Repository initialised with C++ CMake project structure
- XInput backend skeleton, DeadzoneFilter, HapticsEngine
- README, CONTRIBUTING, LICENSE (MIT), .gitignore
- vcpkg manifest (`vcpkg.json`)
