[CmdletBinding()]
param(
    [ValidateSet('Release','Debug')]
    [string]$Config = 'Release',
    [switch]$Run,
    [switch]$Clean,
    [string]$VcpkgRoot = $env:VCPKG_ROOT
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Step  { param([string]$m) Write-Host ('[BUILD] ' + $m) -ForegroundColor Cyan }
function OK    { param([string]$m) Write-Host ('[OK]    ' + $m) -ForegroundColor Green }
function Fail  { param([string]$m) Write-Host ('[FAIL]  ' + $m) -ForegroundColor Red }
function Info  { param([string]$m) Write-Host ('[INFO]  ' + $m) -ForegroundColor DarkGray }
function Warn  { param([string]$m) Write-Host ('[WARN]  ' + $m) -ForegroundColor Yellow }

$RepoRoot = Split-Path $PSScriptRoot -Parent
Set-Location $RepoRoot
Step ('Repository root: ' + $RepoRoot)

Step 'Detecting Visual Studio...'
$VswherePaths = @(
    ($env:ProgramFiles + ' (x86)\Microsoft Visual Studio\Installer\vswhere.exe'),
    ($env:ProgramFiles  + '\Microsoft Visual Studio\Installer\vswhere.exe')
)
$Vswhere = $VswherePaths | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $Vswhere) {
    Fail 'vswhere.exe not found. Install Visual Studio 2019 or 2022 Build Tools.'
    exit 1
}

$VsInstall = (& $Vswhere -latest -products '*' -requires 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' -property installationPath) | Select-Object -First 1
if (-not $VsInstall) {
    Fail 'No VS with C++ tools found.'
    exit 1
}

$VsVersion = (& $Vswhere -latest -products '*' -requires 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' -property installationVersion) | Select-Object -First 1
$Major = [int]($VsVersion -split '\.')[0]

if ($Major -eq 17) { $Generator = 'Visual Studio 17 2022' }
elseif ($Major -eq 16) { $Generator = 'Visual Studio 16 2019' }
else { Fail ('Unsupported VS version: ' + $Major); exit 1 }

OK ('Found: ' + $Generator + '  (' + $VsInstall + ')')

$BuildDir = Join-Path $RepoRoot ('build\' + $Config)

if ($Clean -and (Test-Path $BuildDir)) {
    Step 'Cleaning build directory...'
    Remove-Item $BuildDir -Recurse -Force
    OK ('Cleaned: ' + $BuildDir)
}

$ToolchainArg = ''
if ($VcpkgRoot -and (Test-Path $VcpkgRoot)) {
    $tcFile = Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
    if (Test-Path $tcFile) {
        $ToolchainArg = '-DCMAKE_TOOLCHAIN_FILE=' + ($tcFile -replace '\\', '/')
        OK ('vcpkg toolchain: ' + $tcFile)
    }
} else {
    Warn 'VCPKG_ROOT not set - building without vcpkg.'
}

$cmakeExe = 'cmake'
$BundledCMake = Join-Path $VsInstall 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (Test-Path $BundledCMake) {
    $cmakeExe = $BundledCMake
    Info ('Using bundled CMake: ' + $cmakeExe)
} else {
    Info 'Using system cmake.'
}

Step ('Configuring (' + $Config + ')...')
$ConfigArgs = @(
    '-S', $RepoRoot,
    '-B', $BuildDir,
    '-G', $Generator,
    '-A', 'x64',
    ('-DCMAKE_BUILD_TYPE=' + $Config),
    '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
    '-DENJOYSTICK_BUILD_TESTS=OFF'
)
if ($ToolchainArg) { $ConfigArgs += $ToolchainArg }

& $cmakeExe @ConfigArgs
if ($LASTEXITCODE -ne 0) { Fail ('CMake configure failed (exit ' + $LASTEXITCODE + ').'); exit $LASTEXITCODE }
OK 'Configure complete.'

Step ('Building (' + $Config + ')...')
& $cmakeExe --build $BuildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { Fail ('Build failed (exit ' + $LASTEXITCODE + ').'); exit $LASTEXITCODE }
OK 'Build complete.'

$ExePaths = @(
    (Join-Path $BuildDir ($Config + '\EnjoyStick.exe')),
    (Join-Path $BuildDir 'EnjoyStick.exe')
)
$ExePath = $ExePaths | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $ExePath) {
    Warn 'EnjoyStick.exe not found in expected locations:'
    foreach ($p in $ExePaths) { Write-Host ('         ' + $p) }
} else {
    OK ('Output: ' + $ExePath)
    if ($Run) {
        Step 'Launching EnjoyStick...'
        Start-Process -FilePath $ExePath
        OK 'Launched.'
    } else {
        Write-Host ''
        Write-Host ('  To run:  Start-Process ' + $ExePath) -ForegroundColor DarkGray
        Write-Host ''
    }
}
