<#
.SYNOPSIS
    Configure and build EnjoyStick using CMake + Visual Studio 2019/2022.

.PARAMETER Config
    Build configuration: Debug | Release  (default: Release)

.PARAMETER Clean
    Delete the CMake binary directory before configuring.

.PARAMETER Run
    Launch EnjoyStick.exe after a successful build.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')]
    [string]$Config = 'Release',

    [switch]$Clean,

    [switch]$Run
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
function OK   { param([string]$msg) Write-Host ('[OK]    ' + $msg) -ForegroundColor Green  }
function FAIL { param([string]$msg) Write-Host ('[FAIL]  ' + $msg) -ForegroundColor Red    }
function INFO { param([string]$msg) Write-Host ('[INFO]  ' + $msg) -ForegroundColor Cyan   }
function STEP { param([string]$msg) Write-Host ('[STEP]  ' + $msg) -ForegroundColor Yellow }

# ---------------------------------------------------------------------------
# Locate cmake.exe
#   1. Whatever is already on PATH
#   2. Bundled cmake inside VS2019 / VS2022
# ---------------------------------------------------------------------------
STEP 'Locating cmake...'

$CMake = $null
try {
    $CMake = (Get-Command cmake -ErrorAction Stop).Source
    OK ('cmake (PATH): ' + $CMake)
} catch {
    # Try bundled cmake inside Visual Studio
    $VsWherePaths = @(
        "${Env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
        "${Env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
    )
    $VsWhere = $VsWherePaths | Where-Object { Test-Path $_ } | Select-Object -First 1
    if ($VsWhere) {
        $VsRoot = & $VsWhere -latest -products '*' `
            -requires 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' `
            -property installationPath 2>$null
        if ($VsRoot) {
            $BundledCmake = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path $BundledCmake) {
                $CMake = $BundledCmake
                OK ('cmake (VS bundled): ' + $CMake)
            }
        }
    }
}

if (-not $CMake) {
    FAIL 'cmake.exe not found. Install cmake from https://cmake.org/download/ or add it to PATH.'
    exit 1
}

# ---------------------------------------------------------------------------
# Resolve source root (repo root = parent of scripts\ dir)
# ---------------------------------------------------------------------------
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path -Parent $ScriptDir

if (-not (Test-Path (Join-Path $RepoRoot 'CMakeLists.txt'))) {
    FAIL ('CMakeLists.txt not found at: ' + $RepoRoot)
    FAIL 'Make sure scripts\ is one level inside the repository root.'
    exit 1
}
INFO ('Repository root: ' + $RepoRoot)

# ---------------------------------------------------------------------------
# Select CMake preset based on -Config
# ---------------------------------------------------------------------------
$Preset = if ($Config -eq 'Debug') { 'windows-debug' } else { 'windows-release' }
$BinDir = Join-Path $RepoRoot ('build\' + $Preset)

# Expected exe location for both VS generator multi-config layouts
$ExeCandidates = @(
    (Join-Path $BinDir ($Config + '\EnjoyStick.exe')),   # VS multi-config
    (Join-Path $BinDir 'EnjoyStick.exe')                  # Ninja single-config
)

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------
if ($Clean -and (Test-Path $BinDir)) {
    STEP ('Cleaning: ' + $BinDir)
    Remove-Item -Recurse -Force $BinDir
    OK 'Clean done.'
}

# ---------------------------------------------------------------------------
# CMake Configure
# ---------------------------------------------------------------------------
STEP ('Configuring preset: ' + $Preset)

# Check whether this cmake supports --preset (requires CMake >= 3.20)
$cmakeVersion = (& $CMake --version 2>&1 | Select-Object -First 1).ToString()
$supportsPreset = $cmakeVersion -match '3\.([2-9][0-9]|[4-9][0-9])' -or `
                  $cmakeVersion -match '([4-9]|[1-9][0-9])\.[0-9]'

if ($supportsPreset) {
    & $CMake --preset $Preset -S $RepoRoot
} else {
    # Fallback: manual configure (CMake < 3.20)
    INFO 'cmake --preset not supported; falling back to manual configure.'
    $Generator = 'Visual Studio 16 2019'
    New-Item -ItemType Directory -Force -Path $BinDir | Out-Null
    & $CMake -G $Generator -A x64 `
        -DCMAKE_BUILD_TYPE=$Config `
        -S $RepoRoot -B $BinDir
}

if ($LASTEXITCODE -ne 0) {
    FAIL ('CMake configure failed with exit code ' + $LASTEXITCODE)
    exit $LASTEXITCODE
}
OK 'Configure complete.'

# ---------------------------------------------------------------------------
# CMake Build
# ---------------------------------------------------------------------------
STEP ('Building configuration: ' + $Config)

if ($supportsPreset) {
    & $CMake --build --preset $Preset
} else {
    & $CMake --build $BinDir --config $Config --parallel
}

if ($LASTEXITCODE -ne 0) {
    FAIL ('Build failed with exit code ' + $LASTEXITCODE)
    exit $LASTEXITCODE
}
OK 'Build succeeded.'

# ---------------------------------------------------------------------------
# Report / Launch
# ---------------------------------------------------------------------------
$ExePath = $ExeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

Write-Host ''
if ($ExePath) {
    INFO ('Output: ' + $ExePath)
    if ($Run) {
        INFO 'Launching EnjoyStick...'
        Start-Process -FilePath $ExePath
    } else {
        INFO 'To launch:'
        Write-Host ('  Start-Process ' + "'" + $ExePath + "'") -ForegroundColor DarkGray
    }
} else {
    INFO ('Build dir: ' + $BinDir)
    INFO 'EnjoyStick.exe not found in expected locations (build may use a different layout).'
}
