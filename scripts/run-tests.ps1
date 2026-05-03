#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Run all test suites (C++ GTest + .NET xUnit).
#>
[CmdletBinding()]
param(
    [string]$Preset = 'windows-debug',
    [switch]$Coverage
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Write-Host "▶ Building ($Preset)..." -ForegroundColor Cyan
cmake --preset $Preset
cmake --build "build/$Preset" --config Debug

Write-Host "▶ Running C++ tests..." -ForegroundColor Cyan
ctest --preset $Preset --output-on-failure

Write-Host "▶ Running .NET tests..." -ForegroundColor Cyan
dotnet test tests/Enjoystick.Tests.Shell/ --logger 'console;verbosity=normal'
dotnet test tests/Enjoystick.Tests.InputRouter/ --logger 'console;verbosity=normal'

Write-Host "
✅ All tests passed." -ForegroundColor Green
