<#
.SYNOPSIS
    EnjoyStick Windows — one-click build and optional run script.

.DESCRIPTION
    Locates Visual Studio (2019 or 2022) automatically, configures the CMake
    project with the Visual Studio generator (no Ninja required), builds, and
    optionally launches the resulting EnjoyStick.exe.

    Run from the repository root:
        .\scripts\build.ps1
        .\scripts\build.ps1 -Config Debug
        .\scripts\build.ps1 -Config Release -Run
        .\scripts\build.ps1 -Clean

.PARAMETER Config
    Build configuration: Release (default) or Debug.

.PARAMETER Run
    After a successful build, launch EnjoyStick.exe.

.PARAMETER Clean
    Delete the build directory before configuring.

.PARAMETER VcpkgRoot
    Optional path to a vcpkg installation. If omitted, the VCPKG_ROOT
    environment variable is used. If neither is set, vcpkg is skipped.
#>
[CmdletBinding()]
param(
    [ValidateSet('Release','Debug')]
    [string]$Config    = 'Release',

    [switch]$Run,
    [switch]$Clean,

    [string]$VcpkgRoot = $env:VCPKG_ROOT
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
function Write-Step([string]$msg) {
    Write-Host ""`n[BUILD] $msg"" -ForegroundColor Cyan
}
function Write-OK([string]$msg) {
    Write-Host ""[OK]    $msg"" -ForegroundColor Green
}
function Write-Fail([string]$msg) {
    Write-Host ""[FAIL]  $msg"" -ForegroundColor Red
}

# ---------------------------------------------------------------------------
# Locate repository root (script lives in /scripts)
# ---------------------------------------------------------------------------
$RepoRoot = Split-Path $PSScriptRoot -Parent
Set-Location $RepoRoot
Write-Step "Repository root: $RepoRoot"

# ---------------------------------------------------------------------------
# Find Visual Studio
# ---------------------------------------------------------------------------
Write-Step "Detecting Visual Studio installation..."

$VswherePaths = @(
    "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
    "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
)
$Vswhere = $VswherePaths | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $Vswhere) {
    Write-Fail "vswhere.exe not found. Please install Visual Studio 2019 or 2022 Build Tools."
    exit 1
}

# Prefer VS 2022, fall back to VS 2019
$VsInstall = & $Vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath 2>$null | Select-Object -First 1

if (-not $VsInstall) {
    Write-Fail "No VS installation with C++ tools found."
    exit 1
}

# Determine generator string from VS version
$VsVersion = & $Vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationVersion 2>$null | Select-Object -First 1
$Major = [int]($VsVersion -split '\.')[0]
$Generator = switch ($Major) {
    17 { 'Visual Studio 17 2022' }
    16 { 'Visual Studio 16 2019' }
    default {
        Write-Fail "Unsupported VS version: $Major"
        exit 1
    }
}
Write-OK "Found: $Generator  ($VsInstall)"

# ---------------------------------------------------------------------------
# Build directory
# ---------------------------------------------------------------------------
$BuildDir = Join-Path $RepoRoot "build\$Config"

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Step "Cleaning build directory..."
    Remove-Item $BuildDir -Recurse -Force
    Write-OK "Cleaned."
}

# ---------------------------------------------------------------------------
# vcpkg toolchain (optional)
# ---------------------------------------------------------------------------
$ToolchainArg = ''
if ($VcpkgRoot -and (Test-Path $VcpkgRoot)) {
    $tcFile = Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
    if (Test-Path $tcFile) {
        $ToolchainArg = "-DCMAKE_TOOLCHAIN_FILE=$($tcFile -replace '\\','/')"
        Write-OK "vcpkg toolchain: $tcFile"
    }
} else {
    Write-Host "[INFO]  VCPKG_ROOT not set — building without vcpkg." -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# CMake Configure
# ---------------------------------------------------------------------------
Write-Step "Configuring (CMake)..."
$cmakeExe = 'cmake'
# Prefer CMake bundled with VS if system cmake is too old
$BundledCMake = Join-Path $VsInstall `
    'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (Test-Path $BundledCMake) {
    $cmakeExe = $BundledCMake
    Write-Host "[INFO]  Using bundled CMake: $cmakeExe" -ForegroundColor DarkGray
}

$ConfigArgs = @(
    '-S', $RepoRoot,
    '-B', $BuildDir,
    '-G', $Generator,
    '-A', 'x64',
    "-DCMAKE_BUILD_TYPE=$Config",
    '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
    '-DENJOYSTICK_BUILD_TESTS=OFF'
)
if ($ToolchainArg) { $ConfigArgs += $ToolchainArg }

& $cmakeExe @ConfigArgs
if ($LASTEXITCODE -ne 0) {
    Write-Fail "CMake configure failed (exit $LASTEXITCODE)."
    exit $LASTEXITCODE
}
Write-OK "Configure complete."

# ---------------------------------------------------------------------------
# CMake Build
# ---------------------------------------------------------------------------
Write-Step "Building ($Config)..."
& $cmakeExe --build $BuildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) {
    Write-Fail "Build failed (exit $LASTEXITCODE)."
    exit $LASTEXITCODE
}
Write-OK "Build complete."

# ---------------------------------------------------------------------------
# Locate output exe
# ---------------------------------------------------------------------------
$ExePaths = @(
    (Join-Path $BuildDir "$Config\EnjoyStick.exe"),
    (Join-Path $BuildDir 'EnjoyStick.exe')
)
$ExePath = $ExePaths | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $ExePath) {
    Write-Host "[WARN]  EnjoyStick.exe not found in expected locations:" -ForegroundColor Yellow
    $ExePaths | ForEach-Object { Write-Host "         $_" }
} else {
    Write-OK "Output: $ExePath"
    if ($Run) {
        Write-Step "Launching EnjoyStick..."
        Start-Process -FilePath $ExePath
        Write-OK "Launched."
    } else {
        Write-Host ""`n  To run:  Start-Process '$ExePath'"`n" -ForegroundColor DarkGray
    }
}
