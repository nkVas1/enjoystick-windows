<#
.SYNOPSIS
    Remove build artefacts (build\ directory).
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest

function INFO { param([string]$msg) Write-Host ('[INFO]  ' + $msg) -ForegroundColor Cyan  }
function OK   { param([string]$msg) Write-Host ('[OK]    ' + $msg) -ForegroundColor Green }

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path -Parent $ScriptDir
$BuildDir  = Join-Path $RepoRoot 'build'

if (Test-Path $BuildDir) {
    INFO ('Removing: ' + $BuildDir)
    Remove-Item -Recurse -Force $BuildDir
    OK 'Build directory removed.'
} else {
    INFO 'Nothing to clean (build directory does not exist).'
}
