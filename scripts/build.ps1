<#
.SYNOPSIS
    Configure and build EnjoyStick using CMake + Visual Studio 2019/2022.
    Works with any CMake version >= 3.14 including the version bundled
    with VS 2019 Build Tools (typically 3.16 or 3.17).

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
function Write-OK   { param([string]$msg) Write-Host ('[OK]    ' + $msg) -ForegroundColor Green  }
function Write-Fail { param([string]$msg) Write-Host ('[FAIL]  ' + $msg) -ForegroundColor Red    }
function Write-Info { param([string]$msg) Write-Host ('[INFO]  ' + $msg) -ForegroundColor Cyan   }
function Write-Step { param([string]$msg) Write-Host ('[STEP]  ' + $msg) -ForegroundColor Yellow }

# ---------------------------------------------------------------------------
# Locate cmake.exe
#   Priority: PATH > VS2022 bundled > VS2019 bundled
# ---------------------------------------------------------------------------
Write-Step 'Locating cmake...'

$CMake = $null

try {
    $CMake = (Get-Command cmake -ErrorAction Stop).Source
    Write-OK ('cmake (PATH): ' + $CMake)
} catch {
    # Try vswhere to find VS installation root, then look for bundled cmake
    $VsWhereCandidates = @(
        "${Env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
        "${Env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
    )
    $VsWhere = $VsWhereCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

    if ($VsWhere) {
        $VsRoots = & $VsWhere -all -products '*' `
            -requires 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' `
            -property installationPath 2>$null

        if ($VsRoots) {
            foreach ($root in $VsRoots) {
                $candidate = Join-Path $root `
                    'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
                if (Test-Path $candidate) {
                    $CMake = $candidate
                    Write-OK ('cmake (VS bundled): ' + $CMake)
                    break
                }
            }
        }
    }
}

if (-not $CMake) {
    Write-Fail 'cmake.exe not found.'
    Write-Fail 'Options:'
    Write-Fail '  1. Install cmake from https://cmake.org/download/ and add to PATH.'
    Write-Fail '  2. Install "C++ CMake tools for Windows" via VS Installer.'
    exit 1
}

# ---------------------------------------------------------------------------
# Resolve source root  (repo root = parent of scripts\ directory)
# ---------------------------------------------------------------------------
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path -Parent $ScriptDir

if (-not (Test-Path (Join-Path $RepoRoot 'CMakeLists.txt'))) {
    Write-Fail ('CMakeLists.txt not found at: ' + $RepoRoot)
    Write-Fail 'Ensure scripts\ is one level inside the repository root.'
    exit 1
}
Write-Info ('Repository root: ' + $RepoRoot)

# ---------------------------------------------------------------------------
# Determine generator and output directory
# Always use "Visual Studio 16 2019" x64 — no Ninja, no preset required.
# ---------------------------------------------------------------------------
$Generator = 'Visual Studio 16 2019'
$Platform  = 'x64'
$BinDir    = Join-Path $RepoRoot ('build\vs2019-' + $Config.ToLower())

# VS generator is multi-config: the actual exe lives inside a sub-folder
# named after the configuration.
$ExePath = Join-Path $BinDir ($Config + '\EnjoyStick.exe')

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------
if ($Clean -and (Test-Path $BinDir)) {
    Write-Step ('Cleaning: ' + $BinDir)
    Remove-Item -Recurse -Force $BinDir
    Write-OK 'Clean done.'
}

# ---------------------------------------------------------------------------
# CMake Configure
# ---------------------------------------------------------------------------
Write-Step ('Configuring (' + $Generator + ', ' + $Config + ')...')

$configureArgs = @(
    '-G', $Generator,
    '-A', $Platform,
    "-DCMAKE_BUILD_TYPE=$Config",
    '-DENJOYSTICK_BUILD_TESTS=OFF',
    '-S', $RepoRoot,
    '-B', $BinDir
)

& $CMake @configureArgs

if ($LASTEXITCODE -ne 0) {
    Write-Fail ('CMake configure failed (exit code ' + $LASTEXITCODE + ')')
    exit $LASTEXITCODE
}
Write-OK 'Configure complete.'

# ---------------------------------------------------------------------------
# CMake Build
# ---------------------------------------------------------------------------
Write-Step ('Building configuration: ' + $Config)

$buildArgs = @(
    '--build', $BinDir,
    '--config', $Config,
    '--parallel'
)

& $CMake @buildArgs

if ($LASTEXITCODE -ne 0) {
    Write-Fail ('Build failed (exit code ' + $LASTEXITCODE + ')')
    exit $LASTEXITCODE
}
Write-OK 'Build succeeded.'

# ---------------------------------------------------------------------------
# Report and optionally launch
# ---------------------------------------------------------------------------
Write-Host ''
if (Test-Path $ExePath) {
    Write-Info ('Output: ' + $ExePath)
    if ($Run) {
        Write-Info 'Launching EnjoyStick...'
        Start-Process -FilePath $ExePath
    } else {
        Write-Info 'To launch, run:'
        Write-Host ("  Start-Process '" + $ExePath + "'") -ForegroundColor DarkGray
    }
} else {
    Write-Info ('Build dir: ' + $BinDir)
    Write-Info ('Expected exe not found at: ' + $ExePath)
    Write-Info 'The build may have placed the exe elsewhere.  Check the output above.'
}
