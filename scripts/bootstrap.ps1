#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Bootstrap development environment for Enjoystick Windows.
    Installs vcpkg, required tools, and restores dependencies.
#>
[CmdletBinding()]
param(
    [switch]$Clean,
    [string]$VcpkgRoot = "$env:USERPROFILE\vcpkg"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-Step([string]$msg) {
    Write-Host "`n➤ $msg" -ForegroundColor Cyan
}

function Assert-Command([string]$cmd) {
    if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) {
        throw "Required tool '$cmd' not found. Please install it and re-run."
    }
}

Write-Step "Checking prerequisites"
Assert-Command 'git'
Assert-Command 'cmake'
Assert-Command 'ninja'
Assert-Command 'dotnet'

Write-Step "Setting up vcpkg at $VcpkgRoot"
if (-not (Test-Path $VcpkgRoot)) {
    git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
    & "$VcpkgRoot\bootstrap-vcpkg.bat" -disableMetrics
} else {
    Write-Host "  vcpkg found, pulling latest..."
    git -C $VcpkgRoot pull --ff-only
}
$env:VCPKG_ROOT = $VcpkgRoot
[System.Environment]::SetEnvironmentVariable('VCPKG_ROOT', $VcpkgRoot, 'User')

Write-Step "Installing vcpkg dependencies"
& "$VcpkgRoot\vcpkg.exe" install --triplet x64-windows

Write-Step "Restoring .NET packages"
dotnet restore src/shell/Enjoystick.Shell.csproj
dotnet restore src/browserbridge/Enjoystick.BrowserBridge.csproj

Write-Step "Done! Run the following to build:"
Write-Host "  cmake --preset windows-debug" -ForegroundColor Green
Write-Host "  cmake --build build/windows-debug" -ForegroundColor Green
