<#
.SYNOPSIS
    Launch EnjoyStick.exe (most recent Release build).

.DESCRIPTION
    Searches common build output locations and starts the executable.
    If no build is found, prompts to run build.ps1 first.
#>
[CmdletBinding()]
param(
    [ValidateSet('Release','Debug')]
    [string]$Config = 'Release'
)

$RepoRoot = Split-Path $PSScriptRoot -Parent

$Candidates = @(
    Join-Path $RepoRoot "build\$Config\$Config\EnjoyStick.exe",
    Join-Path $RepoRoot "build\$Config\EnjoyStick.exe",
    Join-Path $RepoRoot "build\windows-release\$Config\EnjoyStick.exe",
    Join-Path $RepoRoot "build\ninja-release\EnjoyStick.exe"
)

$Exe = $Candidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $Exe) {
    Write-Host "EnjoyStick.exe not found. Build first:" -ForegroundColor Yellow
    Write-Host "  .\scripts\build.ps1" -ForegroundColor Cyan
    exit 1
}

Write-Host "Launching: $Exe" -ForegroundColor Green
Start-Process -FilePath $Exe
