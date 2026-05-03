# Building EnjoyStick Windows

This guide covers every way to build the project, from a simple double-click
to full IDE integration.

---

## Requirements

| Tool | Version | Notes |
|---|---|---|
| Windows | 10 21H2 or 11 | Target OS; also build OS |
| Visual Studio **Build Tools** | **2019 (16.x)** or 2022 (17.x) | Install the **"Desktop development with C++"** workload |
| CMake | 3.20+ | Already bundled inside VS Build Tools |
| Git | any | |
| PowerShell | 5.1+ | Built into Windows 10/11 |
| vcpkg | optional | Only needed for future third-party packages |

> **Do not set `VCPKG_ROOT` or `CMAKE_TOOLCHAIN_FILE`** unless you have vcpkg
> installed. Leaving them unset is fine — the project has no external
> dependencies yet.

---

## Option 1 — Double-click (easiest)

1. Clone or download the repository.
2. Double-click **`EnjoyStick.bat`** in the repository root.
   - If a pre-built `EnjoyStick.exe` is found, it launches immediately.
   - If not, it offers to build automatically via PowerShell.

---

## Option 2 — PowerShell script (recommended for developers)

Open **PowerShell** (not cmd) in the repository root:

```powershell
# First time only: install vcpkg + Ninja (optional, safe to skip)
.\scripts\install-deps.ps1

# Build Release
.\scripts\build.ps1

# Build Debug
.\scripts\build.ps1 -Config Debug

# Build and run immediately
.\scripts\build.ps1 -Config Release -Run

# Clean + rebuild
.\scripts\build.ps1 -Clean

# Run the last built exe
.\scripts\run.ps1
```

The script auto-detects your Visual Studio installation via `vswhere.exe`
(included with VS 2019/2022 Build Tools).

**Output location:** `build\Release\Release\EnjoyStick.exe`

---

## Option 3 — Developer Command Prompt (manual)

Open **"x64 Native Tools Command Prompt for VS 2019"** (or 2022):

```cmd
cd G:\CODING\enjoystick-windows

:: Configure
cmake -B build\Release -G "Visual Studio 16 2019" -A x64 -DCMAKE_BUILD_TYPE=Release

:: Build
cmake --build build\Release --config Release --parallel

:: Run
build\Release\Release\EnjoyStick.exe
```

---

## Option 4 — VS Code (CMake Tools extension)

1. Open the repository folder in VS Code.
2. Install recommended extensions (VS Code will prompt, or run
   `Extensions: Show Recommended Extensions`).
3. When prompted to select a kit, choose **Visual Studio Build Tools 2019
   Release - amd64** (or 2022 equivalent).
4. Select the preset **"Windows x64 Release (VS 2019)"** from the CMake
   preset picker in the status bar.
5. Press **Ctrl+Shift+B** → **Build Release**.

> ⚠️ **Do not use the `windows-release` preset** if Ninja is not installed.
> Use `windows-debug` or `windows-release` (both now default to the VS
> generator, not Ninja).

Alternatively, use the VS Code task runner (**Ctrl+Shift+P** →
`Tasks: Run Task`):
- **Build Release**
- **Build Debug**
- **Build & Run**
- **Install Dependencies**
- **Clean**

---

## Option 5 — GitHub Actions (CI)

Every push to `main` builds automatically. Download the `EnjoyStick-Release-x64`
artifact from the [Actions tab](https://github.com/nkVas1/enjoystick-windows/actions).

---

## Troubleshooting

### `Could not find toolchain file: /scripts/buildsystems/vcpkg.cmake`

You have `VCPKG_ROOT` set to an empty string or the variable is set but
points to a non-existent directory. Fix:
```powershell
# Remove it entirely if you don’t use vcpkg:
[System.Environment]::SetEnvironmentVariable('VCPKG_ROOT', $null, 'User')
[System.Environment]::SetEnvironmentVariable('VCPKG_ROOT', $null, 'Machine')
```
Then restart VS Code / your terminal.

### `CMake was unable to find a build program corresponding to "Ninja"`

Use the `windows-release` preset (VS generator) instead of `ninja-release`,
or install Ninja via `scripts\install-deps.ps1`.

### `CMake 3.25 or higher is required`

This should no longer happen after the latest commit. If you see it, make
sure you have pulled the latest `main` branch (`git pull`).

### VS Code keeps using wrong preset

Delete `.vscode/CMakeCache.txt` and the `build/` directory, then
re-open VS Code.

---

## Config file location

After first run, settings are stored at:
```
%APPDATA%\Enjoystick\config.json
```
Edit with any text editor. Changes apply live (hot-reload).
