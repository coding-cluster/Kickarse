#requires -Version 5.1
<#
.SYNOPSIS
  Dependency-free fallback installer/uninstaller for Kickarse, for anyone who would rather not
  run Kickarse-Setup.exe. Copies straight out of a dist\ folder laid out as:
    dist\VST3\Kickarse.vst3\...
    dist\VST2\Kickarse.dll
    dist\CLAP\Kickarse.clap
    dist\LICENSE.txt (or README.txt)  -- optional

  This mirrors Kickarse-Setup.exe's behaviour (same fixed VST3/CLAP locations, same manifest-based
  uninstall, same "don't touch Documents\Kickarse" rule) but needs no compiler, just PowerShell
  5.1+. "Install Kickarse.bat" runs this elevated for a normal (registry-recorded) install; you
  can also run it yourself from an elevated prompt, or with -NoReg from a regular one.

.PARAMETER DistDir
  Folder containing VST3\, VST2\, CLAP\ (defaults to ..\dist next to this script).
.PARAMETER Vst3Dir / Vst2Dir / ClapDir
  Override the fixed install locations (mainly for testing without admin rights).
.PARAMETER Components
  Subset of @('VST3','VST2','CLAP') to install. Defaults to all three.
.PARAMETER NoReg
  Skip the registry uninstall entry and the %ProgramFiles%\Kickarse app folder -- for
  no-admin test installs. Combine with -Vst3Dir/-Vst2Dir/-ClapDir pointing at writable folders.
.PARAMETER ManifestPath
  Where to record (install) / read (uninstall) the list of installed paths. Defaults to
  %ProgramFiles%\Kickarse\install-manifest.txt, or %TEMP%\KickarseInstallManifest.txt if -NoReg.
.PARAMETER Uninstall
  Remove exactly what an earlier install recorded in the manifest. Never touches
  Documents\Kickarse\Presets or \Shapes.
.PARAMETER Silent
  Suppress the human-readable progress/summary output (still throws/exits non-zero on error).
#>
[CmdletBinding()]
param(
    [string]$DistDir = (Join-Path $PSScriptRoot "..\dist"),
    [string]$Vst3Dir,
    [string]$Vst2Dir,
    [string]$ClapDir,
    [string[]]$Components = @('VST3', 'VST2', 'CLAP'),
    [switch]$NoReg,
    [string]$ManifestPath,
    [switch]$Uninstall,
    [switch]$Silent
)

$ErrorActionPreference = 'Stop'

function Write-Info($msg) {
    if (-not $Silent) { Write-Host $msg }
}

function Get-DefaultDirs {
    [PSCustomObject]@{
        Vst3   = Join-Path $env:CommonProgramFiles 'VST3'
        Vst2   = Join-Path $env:CommonProgramFiles 'VST2'
        Clap   = Join-Path $env:CommonProgramFiles 'CLAP'
        AppDir = Join-Path $env:ProgramFiles 'Kickarse'
    }
}

function Remove-DirSafely([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return }
    try {
        Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction Stop
    } catch {
        throw "Setup could not remove `"$path`" because it is in use by another program.`r`n`r`n" +
              "Please close your DAW (or anything else that has Kickarse loaded) and try again."
    }
}

function Remove-FileSafely([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { return }
    try {
        Remove-Item -LiteralPath $path -Force -ErrorAction Stop
    } catch {
        throw "Setup could not remove `"$path`" because it is in use by another program.`r`n`r`n" +
              "Please close your DAW (or anything else that has Kickarse loaded) and try again."
    }
}

function Copy-FileSafely([string]$src, [string]$dst) {
    $dstDir = Split-Path -Parent $dst
    if ($dstDir -and -not (Test-Path -LiteralPath $dstDir)) {
        New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
    }
    try {
        Copy-Item -LiteralPath $src -Destination $dst -Force -ErrorAction Stop
    } catch {
        throw "Setup could not write `"$dst`" because it is in use by another program.`r`n`r`n" +
              "Please close your DAW (or anything else that has Kickarse loaded) and try again."
    }
}

function Invoke-Install {
    $defaults = Get-DefaultDirs
    if (-not $Vst3Dir) { $Vst3Dir = $defaults.Vst3 }
    if (-not $Vst2Dir) { $Vst2Dir = $defaults.Vst2 }
    if (-not $ClapDir) { $ClapDir = $defaults.Clap }

    $doVst3 = $Components -contains 'VST3'
    $doVst2 = $Components -contains 'VST2'
    $doClap = $Components -contains 'CLAP'
    if (-not ($doVst3 -or $doVst2 -or $doClap)) {
        throw "No components selected (Components was: $($Components -join ', '))."
    }

    if (-not (Test-Path -LiteralPath $DistDir)) { throw "Payload folder not found: $DistDir" }

    # user data -- always created, never touched by uninstall
    $docsKickarse = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Kickarse'
    New-Item -ItemType Directory -Force -Path (Join-Path $docsKickarse 'Presets') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $docsKickarse 'Shapes') | Out-Null

    $installedVst3 = $null
    $installedVst2 = $null
    $installedClap = $null
    $appDir = $null

    if ($doVst3) {
        $src = Join-Path $DistDir 'VST3\Kickarse.vst3'
        if (-not (Test-Path -LiteralPath $src)) { throw "Missing $src" }
        $dst = Join-Path $Vst3Dir 'Kickarse.vst3'
        Remove-DirSafely $dst
        New-Item -ItemType Directory -Force -Path $Vst3Dir | Out-Null
        Copy-Item -LiteralPath $src -Destination $dst -Recurse -Force
        $installedVst3 = $dst
        Write-Info "Installed VST3 -> $dst"
    }
    if ($doVst2) {
        $src = Join-Path $DistDir 'VST2\Kickarse.dll'
        if (-not (Test-Path -LiteralPath $src)) { throw "Missing $src" }
        $dst = Join-Path $Vst2Dir 'Kickarse.dll'
        Copy-FileSafely $src $dst
        $installedVst2 = $dst
        Write-Info "Installed VST2 -> $dst"
    }
    if ($doClap) {
        $src = Join-Path $DistDir 'CLAP\Kickarse.clap'
        if (-not (Test-Path -LiteralPath $src)) { throw "Missing $src" }
        $dst = Join-Path $ClapDir 'Kickarse.clap'
        Copy-FileSafely $src $dst
        $installedClap = $dst
        Write-Info "Installed CLAP -> $dst"
    }

    if (-not $NoReg) {
        $appDir = $defaults.AppDir
        New-Item -ItemType Directory -Force -Path $appDir | Out-Null

        foreach ($name in @('LICENSE.txt', 'README.txt')) {
            $docSrc = Join-Path $DistDir $name
            if (Test-Path -LiteralPath $docSrc) {
                Copy-Item -LiteralPath $docSrc -Destination (Join-Path $appDir $name) -Force
                break
            }
        }

        $scriptCopy = Join-Path $appDir 'Install-Kickarse.ps1'
        Copy-Item -LiteralPath $PSCommandPath -Destination $scriptCopy -Force

        $uninstallCmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$scriptCopy`" -Uninstall"
        $keyPath = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Kickarse'
        New-Item -Path $keyPath -Force | Out-Null
        Set-ItemProperty -Path $keyPath -Name DisplayName -Value 'Kickarse'
        Set-ItemProperty -Path $keyPath -Name Publisher -Value 'Kickarse'
        Set-ItemProperty -Path $keyPath -Name InstallLocation -Value $appDir
        Set-ItemProperty -Path $keyPath -Name UninstallString -Value $uninstallCmd
        Set-ItemProperty -Path $keyPath -Name QuietUninstallString -Value ($uninstallCmd + ' -Silent')
        Set-ItemProperty -Path $keyPath -Name DisplayIcon -Value $scriptCopy
        Set-ItemProperty -Path $keyPath -Name NoModify -Value 1 -Type DWord
        Set-ItemProperty -Path $keyPath -Name NoRepair -Value 1 -Type DWord
    }

    if (-not $ManifestPath) {
        $ManifestPath = if ($NoReg) { Join-Path $env:TEMP 'KickarseInstallManifest.txt' }
                        else { Join-Path $defaults.AppDir 'install-manifest.txt' }
    }
    $manifestDir = Split-Path -Parent $ManifestPath
    if ($manifestDir -and -not (Test-Path -LiteralPath $manifestDir)) {
        New-Item -ItemType Directory -Force -Path $manifestDir | Out-Null
    }
    $lines = @('# Kickarse install manifest v1')
    if ($installedVst3) { $lines += "DIR`tVST3`t$installedVst3" }
    if ($installedVst2) { $lines += "FILE`tVST2`t$installedVst2" }
    if ($installedClap) { $lines += "FILE`tCLAP`t$installedClap" }
    if ($appDir) { $lines += "DIR`tAPPDIR`t$appDir" }
    Set-Content -LiteralPath $ManifestPath -Value $lines -Encoding UTF8

    Write-Info ''
    Write-Info 'Kickarse was installed successfully.'
    Write-Info 'Tip: rescan plug-ins in your DAW so it picks up the new install.'
}

function Invoke-Uninstall {
    if (-not $ManifestPath) {
        $ManifestPath = Join-Path (Join-Path $env:ProgramFiles 'Kickarse') 'install-manifest.txt'
    }
    if (-not (Test-Path -LiteralPath $ManifestPath)) {
        throw "Install manifest not found: $ManifestPath`r`n(pass -ManifestPath <file> if this was installed with -NoReg)"
    }

    $appDir = $null
    foreach ($line in (Get-Content -LiteralPath $ManifestPath)) {
        if ([string]::IsNullOrWhiteSpace($line) -or $line.StartsWith('#')) { continue }
        $parts = $line -split "`t"
        if ($parts.Count -lt 3) { continue }
        $type, $tag, $path = $parts[0], $parts[1], $parts[2]
        if ($tag -eq 'APPDIR') { $appDir = $path; continue } # handled last
        if ($type -eq 'DIR') { Remove-DirSafely $path } else { Remove-FileSafely $path }
    }

    try {
        Remove-Item -Path 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Kickarse' `
                    -Recurse -Force -ErrorAction SilentlyContinue
    } catch { }

    if ($appDir) { Remove-DirSafely $appDir } # safe: unlike a running .exe, a .ps1 can delete its own folder
    Remove-Item -LiteralPath $ManifestPath -Force -ErrorAction SilentlyContinue

    Write-Info 'Kickarse has been uninstalled. Your presets and shapes in Documents\Kickarse were kept.'
}

try {
    if ($Uninstall) { Invoke-Uninstall } else { Invoke-Install }
    exit 0
} catch {
    Write-Error $_.Exception.Message
    exit 1
}
