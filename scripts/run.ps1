<#
.SYNOPSIS
    Launch EnjoyStick.exe from the build output directory.
.PARAMETER Config
    Build configuration to run: Debug | Release (default: Release)
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')]
    [string]$Config = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function INFO { param([string]$msg) Write-Host ('[INFO]  ' + $msg) -ForegroundColor Cyan }
function FAIL { param([string]$msg) Write-Host ('[FAIL]  ' + $msg) -ForegroundColor Red  }

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path -Parent $ScriptDir
$ExePath   = Join-Path $RepoRoot ('build\' + 'EnjoyStick.exe')

if (-not (Test-Path $ExePath)) {
    FAIL ('EnjoyStick.exe not found at: ' + $ExePath)
    FAIL 'Run scripts\build.ps1 first.'
    exit 1
}

INFO ('Launching: ' + $ExePath)
Start-Process -FilePath $ExePath
