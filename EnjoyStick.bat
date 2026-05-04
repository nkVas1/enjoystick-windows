@echo off
SETLOCAL ENABLEDELAYEDEXPANSION

:: ============================================================
:: EnjoyStick.bat -- build (if needed) and run EnjoyStick
:: Place this file in the repository root and double-click it.
:: ============================================================

echo.
echo  ==========================================
echo   EnjoyStick Windows -- Launcher
echo  ==========================================
echo.

:: --- Verify PowerShell is available ---
where powershell >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] PowerShell not found.
    pause
    exit /b 1
)

:: --- Search for a pre-built exe (all possible CMake preset output dirs) ---
::
:: CMakePresets.json binaryDir layout:
::   build/windows-release/Release/EnjoyStick.exe   (VS multi-config)
::   build/windows-release/EnjoyStick.exe           (Ninja single-config)
::   build/windows-debug/Debug/EnjoyStick.exe
::   build/ninja-release/EnjoyStick.exe
::
set FOUND_EXE=

for %%P in (
    "%~dp0build\windows-release\Release\EnjoyStick.exe"
    "%~dp0build\windows-release\EnjoyStick.exe"
    "%~dp0build\ninja-release\EnjoyStick.exe"
    "%~dp0build\windows-debug\Debug\EnjoyStick.exe"
    "%~dp0build\windows-debug\EnjoyStick.exe"
) do (
    if exist %%P (
        set FOUND_EXE=%%P
        goto :found
    )
)

:: --- No exe found: offer to build ---
echo  No pre-built EnjoyStick.exe found.
echo.
choice /C YN /M "Build now with Visual Studio 2019/2022?"
if %ERRORLEVEL% EQU 2 (
    echo  Cancelled.
    pause
    exit /b 0
)

echo.
echo  Running CMake build script...
powershell -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" -Config Release
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo  [ERROR] Build failed. See output above.
    pause
    exit /b %ERRORLEVEL%
)

:: --- After build: search again ---
set FOUND_EXE=
for %%P in (
    "%~dp0build\windows-release\Release\EnjoyStick.exe"
    "%~dp0build\windows-release\EnjoyStick.exe"
    "%~dp0build\ninja-release\EnjoyStick.exe"
) do (
    if exist %%P (
        set FOUND_EXE=%%P
        goto :found
    )
)

echo.
echo  Build completed but EnjoyStick.exe was not found in expected locations.
echo  Check the build output above for the actual path.
pause
ENDLOCAL
exit /b 0

:found
echo  Found: %FOUND_EXE%
echo  Launching EnjoyStick...
start "" %FOUND_EXE%
ENDLOCAL
exit /b 0
