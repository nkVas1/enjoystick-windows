<#
.SYNOPSIS
    Install/verify third-party dependencies required to build EnjoyStick.
    Currently checks for: CMake, Ninja, and vcpkg (optional).
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function OK   { param([string]$msg) Write-Host ('[OK]    ' + $msg) -ForegroundColor Green  }
function FAIL { param([string]$msg) Write-Host ('[FAIL]  ' + $msg) -ForegroundColor Red    }
function INFO { param([string]$msg) Write-Host ('[INFO]  ' + $msg) -ForegroundColor Cyan   }
function SKIP { param([string]$msg) Write-Host ('[SKIP]  ' + $msg) -ForegroundColor Gray   }

# ---------------------------------------------------------------------------
# Check CMake
# ---------------------------------------------------------------------------
INFO 'Checking CMake...'
try {
    $cmake = (Get-Command cmake -ErrorAction Stop).Source
    $ver   = (cmake --version 2>&1 | Select-Object -First 1).ToString().Trim()
    OK ('cmake found: ' + $cmake + ' (' + $ver + ')')
} catch {
    FAIL 'cmake not found. Download from https://cmake.org/download/'
}

# ---------------------------------------------------------------------------
# Check Ninja
# ---------------------------------------------------------------------------
INFO 'Checking Ninja...'
try {
    $ninja = (Get-Command ninja -ErrorAction Stop).Source
    $ver   = (ninja --version 2>&1).ToString().Trim()
    OK ('ninja found: ' + $ninja + ' (' + $ver + ')')
} catch {
    SKIP 'ninja not found (optional — MSBuild generator will be used instead)'
}

# ---------------------------------------------------------------------------
# Check Git
# ---------------------------------------------------------------------------
INFO 'Checking Git...'
try {
    $git = (Get-Command git -ErrorAction Stop).Source
    $ver  = (git --version 2>&1).ToString().Trim()
    OK ('git found: ' + $git + ' (' + $ver + ')')
} catch {
    FAIL 'git not found. Install Git for Windows: https://git-scm.com/'
}

# ---------------------------------------------------------------------------
# Check vcpkg (optional)
# ---------------------------------------------------------------------------
INFO 'Checking vcpkg...'
if ($Env:VCPKG_ROOT -and (Test-Path (Join-Path $Env:VCPKG_ROOT 'vcpkg.exe'))) {
    OK ('vcpkg found: ' + $Env:VCPKG_ROOT)
} else {
    SKIP 'vcpkg not found. Set %VCPKG_ROOT% if you use vcpkg for dependency management.'
}

Write-Host ''
INFO 'Dependency check complete.'
