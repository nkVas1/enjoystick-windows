@echo off
SETLOCAL ENABLEDELAYEDEXPANSION

:: ============================================================
:: EnjoyStick.bat — build (if needed) and run EnjoyStick
:: Place this file in the repository root and double-click it.
:: ============================================================

echo.
echo  ==========================================
echo   EnjoyStick Windows — Launcher
echo  ==========================================
echo.

:: --- Check for PowerShell ---
where powershell >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] PowerShell not found. Please install PowerShell.
    pause
    exit /b 1
)

:: --- Look for a pre-built exe first ---
set EXE_PATHS[0]=%~dp0build\Release\Release\EnjoyStick.exe
set EXE_PATHS[1]=%~dp0build\Release\EnjoyStick.exe
set EXE_PATHS[2]=%~dp0build\windows-release\Release\EnjoyStick.exe
set EXE_PATHS[3]=%~dp0build\ninja-release\EnjoyStick.exe

for /L %%i in (0,1,3) do (
    call set CANDIDATE=%%EXE_PATHS[%%i]%%
    if exist "!CANDIDATE!" (
        echo  Found: !CANDIDATE!
        echo  Launching EnjoyStick...
        start "" "!CANDIDATE!"
        exit /b 0
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
echo  Running build script...
powershell -ExecutionPolicy Bypass -File "%~dp0scripts\build.ps1" -Config Release -Run
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo  [ERROR] Build failed. See output above.
    pause
    exit /b %ERRORLEVEL%
)

ENDLOCAL
