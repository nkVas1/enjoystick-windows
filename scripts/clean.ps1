<#
.SYNOPSIS
    Delete all CMake build directories.
#>
$RepoRoot = Split-Path $PSScriptRoot -Parent
$BuildRoot = Join-Path $RepoRoot 'build'

if (Test-Path $BuildRoot) {
    Remove-Item $BuildRoot -Recurse -Force
    Write-Host "Cleaned: $BuildRoot" -ForegroundColor Green
} else {
    Write-Host "Nothing to clean." -ForegroundColor DarkGray
}
