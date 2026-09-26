#requires -Version 5.1
<#
.SYNOPSIS
  Builds Kickarse-Setup.exe from installer\ via CMake + MSVC and copies it to dist\Kickarse-Setup.exe.

.PARAMETER PayloadDir
  Folder laid out as VST3\Kickarse.vst3\, VST2\<name>.dll, CLAP\<name>.clap, optionally
  LICENSE.txt/README.txt. Defaults to the project's dist\ folder (what build.ps1 produces).

.PARAMETER BuildDir
  Out-of-OneDrive build tree. Defaults to %LOCALAPPDATA%\KickarseBuild\installer.

.EXAMPLE
  .\build-installer.ps1
.EXAMPLE
  .\build-installer.ps1 -PayloadDir C:\temp\fake-dist -BuildDir $env:LOCALAPPDATA\KickarseBuild\installer-test
#>
[CmdletBinding()]
param(
    [string]$PayloadDir = (Join-Path $PSScriptRoot '..\dist'),
    [string]$BuildDir = (Join-Path $env:LOCALAPPDATA 'KickarseBuild\installer'),
    [string]$Config = 'Release',
    [string]$Generator = 'Visual Studio 17 2022',
    [string]$Version
)

$ErrorActionPreference = 'Stop'

function Fail($msg) {
    Write-Host "build-installer: ERROR: $msg" -ForegroundColor Red
    exit 1
}

# GetFullPath handles an already-absolute $PayloadDir correctly on its own; joining it with the
# current directory first (when it's already rooted) produces an invalid combined path on Windows.
if (-not [System.IO.Path]::IsPathRooted($PayloadDir)) {
    $PayloadDir = Join-Path (Get-Location) $PayloadDir
}
$PayloadDir = [System.IO.Path]::GetFullPath($PayloadDir)
$installerDir = $PSScriptRoot

Write-Host "build-installer: payload dir  = $PayloadDir"
Write-Host "build-installer: build dir    = $BuildDir"

# Fail clearly and early -- CMake's own configure-time check would catch this too, but this gives
# a one-shot, no-cmake-noise error message naming every missing piece at once.
$missing = New-Object System.Collections.Generic.List[string]
if (-not (Test-Path -LiteralPath (Join-Path $PayloadDir 'VST3\Kickarse.vst3') -PathType Container)) {
    $missing.Add('VST3\Kickarse.vst3\ (the whole VST3 bundle folder)')
}
if (-not (Get-ChildItem -LiteralPath (Join-Path $PayloadDir 'VST2') -Filter '*.dll' -File -ErrorAction SilentlyContinue)) {
    $missing.Add('VST2\*.dll (exactly one)')
}
if (-not (Get-ChildItem -LiteralPath (Join-Path $PayloadDir 'CLAP') -Filter '*.clap' -File -ErrorAction SilentlyContinue)) {
    $missing.Add('CLAP\*.clap (exactly one)')
}
if ($missing.Count -gt 0) {
    Write-Host "build-installer: payload is missing required files under $PayloadDir :" -ForegroundColor Red
    foreach ($m in $missing) { Write-Host "  - $m" -ForegroundColor Red }
    exit 1
}

$cmakeArgs = @(
    '-S', $installerDir,
    '-B', $BuildDir,
    '-G', $Generator,
    '-A', 'x64',
    "-DKICKARSE_PAYLOAD_DIR=$PayloadDir"
)
if ($Version) { $cmakeArgs += "-DKICKARSE_VERSION=$Version" }

Write-Host "build-installer: configuring..."
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { Fail "cmake configure failed (exit $LASTEXITCODE)" }

Write-Host "build-installer: building ($Config)..."
& cmake --build $BuildDir --config $Config -- /m
if ($LASTEXITCODE -ne 0) { Fail "cmake build failed (exit $LASTEXITCODE)" }

$builtExe = Join-Path $BuildDir 'bin\Kickarse-Setup.exe'
if (-not (Test-Path -LiteralPath $builtExe)) { Fail "expected output not found: $builtExe" }

$distDir = Join-Path (Split-Path -Parent $installerDir) 'dist'
New-Item -ItemType Directory -Force -Path $distDir | Out-Null
$finalExe = Join-Path $distDir 'Kickarse-Setup.exe'
Copy-Item -LiteralPath $builtExe -Destination $finalExe -Force

$sizeKb = [math]::Round((Get-Item $builtExe).Length / 1KB, 1)
Write-Host "build-installer: built Kickarse-Setup.exe ($sizeKb KB) -> $finalExe" -ForegroundColor Green
