<#
.SYNOPSIS
    Build EnjoyStick using MSBuild / Visual Studio 2019 or 2022.
.PARAMETER Config
    Build configuration: Debug | Release (default: Release)
.PARAMETER Platform
    Target platform: x64 | x86 (default: x64)
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')]
    [string]$Config = 'Release',

    [ValidateSet('x64','x86')]
    [string]$Platform = 'x64'
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
# Locate vswhere.exe
# ---------------------------------------------------------------------------
$VsWherePaths = @(
    "${Env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
    "${Env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
)

$VsWhere = $VsWherePaths | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $VsWhere) {
    FAIL 'vswhere.exe not found. Please install Visual Studio 2019 or 2022.'
    exit 1
}

# ---------------------------------------------------------------------------
# Locate MSBuild via vswhere (prefer VS2022, fall back to VS2019)
# ---------------------------------------------------------------------------
STEP 'Locating MSBuild...'

$VsInstallRoot = & $VsWhere -latest `
    -products '*' `
    -requires 'Microsoft.Component.MSBuild' `
    -property 'installationPath' 2>$null

if (-not $VsInstallRoot) {
    FAIL 'No Visual Studio installation with MSBuild found.'
    exit 1
}

$MSBuild = Join-Path $VsInstallRoot 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path $MSBuild)) {
    $MSBuild = Join-Path $VsInstallRoot 'MSBuild\15.0\Bin\MSBuild.exe'
}
if (-not (Test-Path $MSBuild)) {
    FAIL ('MSBuild.exe not found under: ' + $VsInstallRoot)
    exit 1
}
OK ('MSBuild: ' + $MSBuild)

# ---------------------------------------------------------------------------
# Resolve paths
# ---------------------------------------------------------------------------
$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot   = Split-Path -Parent $ScriptDir

# Support both flat-layout (enjoystick-windows.sln at repo root) and
# nested layout (enjoystick-windows/enjoystick-windows.sln).
$SlnCandidates = @(
    (Join-Path $RepoRoot 'enjoystick-windows.sln'),
    (Join-Path $RepoRoot 'enjoystick-windows\enjoystick-windows.sln')
)
$SolutionFile = $SlnCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $SolutionFile) {
    FAIL 'enjoystick-windows.sln not found. Expected at repo root or in enjoystick-windows\'
    exit 1
}
INFO ('Solution: ' + $SolutionFile)

$OutDir = Join-Path $RepoRoot 'build'
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

# ---------------------------------------------------------------------------
# Run MSBuild
# ---------------------------------------------------------------------------
STEP ('Building ' + $Config + '|' + $Platform + '...')

$MSBuildArgs = @(
    $SolutionFile,
    '/m',                                        # parallel build
    '/nologo',
    ('/p:Configuration=' + $Config),
    ('/p:Platform='       + $Platform),
    ('/p:OutDir='         + $OutDir + '\')
)

& $MSBuild @MSBuildArgs
$ExitCode = $LASTEXITCODE

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
if ($ExitCode -eq 0) {
    $ExePath = Join-Path $OutDir 'EnjoyStick.exe'
    OK 'Build succeeded.'
    Write-Host ''
    INFO ('Output: ' + $OutDir)
    if (Test-Path $ExePath) {
        INFO 'To run:'
        Write-Host ('  Start-Process ' + "'" + $ExePath + "'") -ForegroundColor DarkGray
    }
} else {
    FAIL ('MSBuild exited with code ' + $ExitCode)
    exit $ExitCode
}
