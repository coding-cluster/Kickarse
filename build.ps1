#requires -version 5.1
<#
.SYNOPSIS
    Configure + build Kickarse (VST2, VST3, CLAP, standalone) and collect the artefacts into dist\.

.PARAMETER Config
    Debug or Release (default Release).

.PARAMETER Clean
    Delete the build tree first (%LOCALAPPDATA%\KickarseBuild\main).

.PARAMETER Stub
    Build kickarse_core from plugin/dev/CoreStub.cpp (-DKICKARSE_CORE_STUB=ON) instead of src/.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release',

    [switch]$Clean,

    [switch]$Stub
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = $PSScriptRoot
$BuildDir    = Join-Path $env:LOCALAPPDATA 'KickarseBuild\main'
$DistDir     = Join-Path $ProjectRoot 'dist'
$BinDir      = Join-Path $BuildDir 'bin'

Write-Host '==================================================================='
Write-Host ' Kickarse build'
Write-Host "   Config     : $Config"
Write-Host "   Stub core  : $($Stub.IsPresent)"
Write-Host "   Build dir  : $BuildDir"
Write-Host "   Dist dir   : $DistDir"
Write-Host '==================================================================='

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Cleaning $BuildDir ..."
    Remove-Item -Recurse -Force $BuildDir
}

if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

$cmakeArgs = @(
    '-S', $ProjectRoot,
    '-B', $BuildDir,
    '-G', 'Visual Studio 17 2022',
    '-A', 'x64'
)
if ($Stub) {
    $cmakeArgs += '-DKICKARSE_CORE_STUB=ON'
}

Write-Host "`nConfiguring..."
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configure failed (exit code $LASTEXITCODE)"
    exit 1
}

Write-Host "`nBuilding ($Config)..."
& cmake --build $BuildDir --config $Config -- /m
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed (exit code $LASTEXITCODE)"
    exit 1
}

# ---- collect artefacts into dist\ --------------------------------------------------------

function Copy-SingleFile {
    param([string]$Source, [string]$DestDir, [string]$DestName)

    if (-not (Test-Path $Source)) {
        Write-Warning "Missing artefact: $Source"
        return $null
    }
    if (-not (Test-Path $DestDir)) {
        New-Item -ItemType Directory -Path $DestDir -Force | Out-Null
    }
    $destPath = Join-Path $DestDir $DestName
    Copy-Item -Path $Source -Destination $destPath -Force
    return $destPath
}

# Empty dist\ rather than deleting it: an Explorer window or OneDrive often holds the folder itself.
if (Test-Path $DistDir) {
    Get-ChildItem -LiteralPath $DistDir -Force | Remove-Item -Recurse -Force
} else {
    New-Item -ItemType Directory -Path $DistDir | Out-Null
}

$results = [ordered]@{}

# VST3: copy the whole bundle folder (bin\Kickarse.vst3\Contents\<arch>-win\Kickarse.vst3)
$vst3Src = Join-Path $BinDir 'Kickarse.vst3'
if (Test-Path $vst3Src) {
    $vst3ParentDir = Join-Path $DistDir 'VST3'
    New-Item -ItemType Directory -Path $vst3ParentDir -Force | Out-Null
    Copy-Item -Path $vst3Src -Destination (Join-Path $vst3ParentDir 'Kickarse.vst3') -Recurse -Force
    $results['VST3'] = Join-Path $vst3ParentDir 'Kickarse.vst3'
} else {
    Write-Warning "Missing artefact: $vst3Src"
    $results['VST3'] = $null
}

# VST2: DPF names the DLL Kickarse-vst2.dll (to disambiguate it from the other targets in the
# same bin\ dir); rename it to the plain Kickarse.dll a user actually drops into their VST2 folder.
$results['VST2'] = Copy-SingleFile -Source (Join-Path $BinDir 'Kickarse-vst2.dll') `
                                   -DestDir (Join-Path $DistDir 'VST2') `
                                   -DestName 'Kickarse.dll'

# CLAP
$results['CLAP'] = Copy-SingleFile -Source (Join-Path $BinDir 'Kickarse.clap') `
                                   -DestDir (Join-Path $DistDir 'CLAP') `
                                   -DestName 'Kickarse.clap'

# Standalone (jack target, native RtAudio fallback on Windows)
$results['Standalone'] = Copy-SingleFile -Source (Join-Path $BinDir 'Kickarse.exe') `
                                         -DestDir (Join-Path $DistDir 'Standalone') `
                                         -DestName 'Kickarse.exe'

Write-Host "`n==================================================================="
Write-Host ' Build summary'
Write-Host '==================================================================='

$allOk = $true
foreach ($key in $results.Keys) {
    if ($results[$key]) {
        Write-Host ('   {0,-12}: OK  -> {1}' -f $key, $results[$key])
    } else {
        Write-Host ('   {0,-12}: MISSING' -f $key) -ForegroundColor Yellow
        $allOk = $false
    }
}
Write-Host '==================================================================='

if (-not $allOk) {
    Write-Error 'One or more artefacts were missing -- see warnings above.'
    exit 1
}

Write-Host "`nAll artefacts built and copied to $DistDir"
exit 0
