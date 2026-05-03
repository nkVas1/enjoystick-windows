<#
.SYNOPSIS
    Bootstrap EnjoyStick development dependencies.

.DESCRIPTION
    Installs or updates:
      - vcpkg   (cloned to C:\vcpkg, VCPKG_ROOT set in user environment)
      - Ninja   (downloaded to C:\ninja, added to user PATH)

    Safe to re-run: skips any step that is already done.
    Does NOT require administrator rights for user-level PATH changes.

.EXAMPLE
    .\scripts\install-deps.ps1
#>
[CmdletBinding()]
param(
    [string]$VcpkgPath = 'C:\vcpkg',
    [string]$NinjaPath = 'C:\ninja'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Step([string]$m) { Write-Host "`n>>> $m" -ForegroundColor Cyan }
function Write-OK([string]$m)   { Write-Host "    OK: $m" -ForegroundColor Green }
function Write-Skip([string]$m) { Write-Host "    SKIP: $m (already done)" -ForegroundColor DarkGray }

# ---------------------------------------------------------------------------
# 1. vcpkg
# ---------------------------------------------------------------------------
Write-Step 'vcpkg'

if (-not (Test-Path (Join-Path $VcpkgPath 'vcpkg.exe'))) {
    if (Test-Path $VcpkgPath) {
        Write-Host "    Updating existing clone..."
        & git -C $VcpkgPath pull --ff-only
    } else {
        Write-Host "    Cloning vcpkg..."
        & git clone https://github.com/microsoft/vcpkg.git $VcpkgPath
    }
    Write-Host "    Bootstrapping..."
    & (Join-Path $VcpkgPath 'bootstrap-vcpkg.bat') -disableMetrics
    Write-OK "vcpkg built at $VcpkgPath"
} else {
    Write-Skip "vcpkg.exe exists at $VcpkgPath"
}

$currentVcpkg = [System.Environment]::GetEnvironmentVariable('VCPKG_ROOT', 'User')
if ($currentVcpkg -ne $VcpkgPath) {
    [System.Environment]::SetEnvironmentVariable('VCPKG_ROOT', $VcpkgPath, 'User')
    $env:VCPKG_ROOT = $VcpkgPath
    Write-OK "VCPKG_ROOT = $VcpkgPath (user env)"
} else {
    Write-Skip 'VCPKG_ROOT already set'
}

# ---------------------------------------------------------------------------
# 2. Ninja
# ---------------------------------------------------------------------------
Write-Step 'Ninja build tool'

$NinjaExe = Join-Path $NinjaPath 'ninja.exe'
if (-not (Test-Path $NinjaExe)) {
    New-Item -ItemType Directory -Path $NinjaPath -Force | Out-Null

    Write-Host "    Fetching latest Ninja release info..."
    $Release = Invoke-RestMethod 'https://api.github.com/repos/ninja-build/ninja/releases/latest'
    $Asset   = $Release.assets | Where-Object { $_.name -like '*win*' } | Select-Object -First 1

    if (-not $Asset) {
        Write-Host "    [WARN] Could not auto-download Ninja. Visit https://ninja-build.org/" -ForegroundColor Yellow
    } else {
        $Zip = Join-Path $env:TEMP 'ninja-win.zip'
        Write-Host "    Downloading $($Asset.name)..."
        Invoke-WebRequest -Uri $Asset.browser_download_url -OutFile $Zip -UseBasicParsing
        Expand-Archive -Path $Zip -DestinationPath $NinjaPath -Force
        Remove-Item $Zip
        Write-OK "Ninja installed at $NinjaPath"
    }
} else {
    Write-Skip "ninja.exe exists at $NinjaPath"
}

$UserPath = [System.Environment]::GetEnvironmentVariable('PATH', 'User')
if ($UserPath -notlike "*$NinjaPath*") {
    [System.Environment]::SetEnvironmentVariable('PATH', "$UserPath;$NinjaPath", 'User')
    $env:PATH += ";$NinjaPath"
    Write-OK "$NinjaPath added to user PATH"
} else {
    Write-Skip "$NinjaPath already in PATH"
}

# ---------------------------------------------------------------------------
# Done
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "Dependencies ready. Open a NEW terminal for PATH changes to take effect." -ForegroundColor Green
Write-Host "Then run:  .\scripts\build.ps1" -ForegroundColor Cyan
