# EnjoyStick Installer

This directory contains the NSIS installer script for EnjoyStick Windows.

## Prerequisites

1. [NSIS 3.x](https://nsis.sourceforge.io/Download) installed and on your `PATH`
2. A completed Release build: `build\Release\Release\EnjoyStick.exe` must exist

## Building the installer

```powershell
# From the repo root:
.\scripts\build.ps1                  # 1. Build the app first
makensis installer\enjoystick.nsi    # 2. Package into installer
```

Output: `installer\EnjoyStick-Setup-0.1.0.exe`

## What the installer does

- Installs to `%ProgramFiles%\EnjoyStick\`
- Adds EnjoyStick to the Start Menu and Desktop
- Registers in Add/Remove Programs
- Sets auto-start on login (same as the app does automatically)
- Does **not** touch `%APPDATA%\Enjoystick\` (user config is preserved on uninstall)

## Silent install

```
EnjoyStick-Setup-0.1.0.exe /S
```
