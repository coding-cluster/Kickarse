#requires -Version 5.1
<#
.SYNOPSIS
  Packs the Kickarse payload directory (dist\ layout) into two flat binary blobs that get
  embedded into Kickarse-Setup.exe as RCDATA resources: payload.bin (raw file bytes back to
  back) and manifest.bin (a tiny custom table of {category, relative path, offset, size}).

  This is a build-time tool only; nothing here ships inside the installer. It intentionally
  avoids CMake's own file(READ ... HEX) machinery, which is extremely slow on multi-megabyte
  binaries -- plain .NET FileStream/BinaryWriter handles this instantly.

.PARAMETER PayloadDir
  Directory laid out as:
    <PayloadDir>\VST3\Kickarse.vst3\...           (the whole bundle tree)
    <PayloadDir>\VST2\*.dll                       (exactly one file; renamed to Kickarse.dll)
    <PayloadDir>\CLAP\*.clap                      (exactly one file; renamed to Kickarse.clap)
    <PayloadDir>\LICENSE.txt or README.txt        (top level; optional, auto-generated if absent)

.PARAMETER OutDir
  Directory to write payload.bin and manifest.bin into (created if missing).
#>
param(
    [Parameter(Mandatory = $true)][string]$PayloadDir,
    [Parameter(Mandatory = $true)][string]$OutDir
)

$ErrorActionPreference = 'Stop'

function Fail($msg) {
    Write-Host "PackPayload: ERROR: $msg" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path -LiteralPath $PayloadDir -PathType Container)) {
    Fail "payload directory not found: $PayloadDir"
}
$PayloadDir = (Resolve-Path -LiteralPath $PayloadDir).Path
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path -LiteralPath $OutDir).Path

# category codes must match installer/src/Payload.h
$CAT_VST3 = 0
$CAT_VST2 = 1
$CAT_CLAP = 2
$CAT_DOC  = 3

$entries = New-Object System.Collections.Generic.List[Object]

# ---------------------------------------------------------------- VST3 bundle
$vst3Root = Join-Path $PayloadDir 'VST3\Kickarse.vst3'
if (-not (Test-Path -LiteralPath $vst3Root -PathType Container)) {
    Fail "missing VST3 bundle folder: $vst3Root (expected the whole Kickarse.vst3\ tree)"
}
$vst3Files = Get-ChildItem -LiteralPath $vst3Root -Recurse -File
if ($vst3Files.Count -eq 0) {
    Fail "VST3 bundle folder is empty: $vst3Root"
}
foreach ($f in $vst3Files) {
    # Relative to the bundle root itself (Kickarse.vst3\) -- the installer already appends
    # "Kickarse.vst3" once when it joins the chosen VST3 parent dir with these entries, so this
    # must NOT repeat that folder name (that bug produced Kickarse.vst3\Kickarse.vst3\... on disk).
    $rel = $f.FullName.Substring($vst3Root.Length).TrimStart('\')
    $entries.Add([PSCustomObject]@{ Category = $CAT_VST3; RelPath = $rel; SourcePath = $f.FullName })
}

# ------------------------------------------------------------------- VST2 dll
$vst2Dir = Join-Path $PayloadDir 'VST2'
if (-not (Test-Path -LiteralPath $vst2Dir -PathType Container)) {
    Fail "missing VST2 folder: $vst2Dir (expected a single .dll in it)"
}
$vst2Files = @(Get-ChildItem -LiteralPath $vst2Dir -Filter '*.dll' -File)
if ($vst2Files.Count -eq 0) { Fail "no .dll found in $vst2Dir" }
if ($vst2Files.Count -gt 1) { Fail "expected exactly one .dll in $vst2Dir, found $($vst2Files.Count): $($vst2Files.Name -join ', ')" }
$entries.Add([PSCustomObject]@{ Category = $CAT_VST2; RelPath = 'Kickarse.dll'; SourcePath = $vst2Files[0].FullName })

# ------------------------------------------------------------------ CLAP file
$clapDir = Join-Path $PayloadDir 'CLAP'
if (-not (Test-Path -LiteralPath $clapDir -PathType Container)) {
    Fail "missing CLAP folder: $clapDir (expected a single .clap file in it)"
}
$clapFiles = @(Get-ChildItem -LiteralPath $clapDir -Filter '*.clap' -File)
if ($clapFiles.Count -eq 0) { Fail "no .clap file found in $clapDir" }
if ($clapFiles.Count -gt 1) { Fail "expected exactly one .clap file in $clapDir, found $($clapFiles.Count): $($clapFiles.Name -join ', ')" }
$entries.Add([PSCustomObject]@{ Category = $CAT_CLAP; RelPath = 'Kickarse.clap'; SourcePath = $clapFiles[0].FullName })

# --------------------------------------------------------------- LICENSE/README
$docFile = $null
foreach ($name in @('LICENSE.txt', 'README.txt', 'License.txt', 'Readme.txt')) {
    $candidate = Join-Path $PayloadDir $name
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $docFile = $candidate; break }
}
if (-not $docFile) {
    Write-Host "PackPayload: no LICENSE.txt/README.txt found at $PayloadDir, generating a minimal placeholder."
    $docFile = Join-Path $OutDir 'LICENSE.txt'
    @(
        'Kickarse'
        ''
        'Thank you for installing Kickarse.'
        'See the project documentation for license terms.'
    ) -join "`r`n" | Set-Content -LiteralPath $docFile -Encoding UTF8
}
$entries.Add([PSCustomObject]@{ Category = $CAT_DOC; RelPath = (Split-Path -Leaf $docFile); SourcePath = $docFile })

# ------------------------------------------------------------------- pack it
$payloadPath  = Join-Path $OutDir 'payload.bin'
$manifestPath = Join-Path $OutDir 'manifest.bin'

$offsets = New-Object System.Collections.Generic.List[UInt64]
$sizes   = New-Object System.Collections.Generic.List[UInt64]

$payloadStream = [System.IO.File]::Open($payloadPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
try {
    [UInt64]$cursor = 0
    foreach ($e in $entries) {
        $bytes = [System.IO.File]::ReadAllBytes($e.SourcePath)
        $payloadStream.Write($bytes, 0, $bytes.Length)
        $offsets.Add($cursor)
        $sizes.Add([UInt64]$bytes.Length)
        $cursor += [UInt64]$bytes.Length
    }
} finally {
    $payloadStream.Close()
}

$manifestStream = [System.IO.File]::Open($manifestPath, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
$bw = New-Object System.IO.BinaryWriter($manifestStream)
try {
    $bw.Write([byte[]][char[]]'KKPK')      # magic
    $bw.Write([UInt32]1)                    # format version
    $bw.Write([UInt32]$entries.Count)       # entry count
    for ($i = 0; $i -lt $entries.Count; $i++) {
        $e = $entries[$i]
        $bw.Write([byte]$e.Category)
        $bw.Write([byte[]]@(0,0,0))          # padding to align next field
        $relBytes = [System.Text.Encoding]::UTF8.GetBytes($e.RelPath.Replace('/', '\'))
        $bw.Write([UInt32]$relBytes.Length)
        $bw.Write($relBytes)
        $bw.Write([UInt64]$offsets[$i])
        $bw.Write([UInt64]$sizes[$i])
    }
} finally {
    $bw.Close()
    $manifestStream.Close()
}

$totalBytes = (Get-Item $payloadPath).Length
Write-Host "PackPayload: packed $($entries.Count) entries, $totalBytes bytes of payload -> $payloadPath / $manifestPath"
foreach ($e in $entries) {
    $catName = switch ($e.Category) { 0 {'VST3'} 1 {'VST2'} 2 {'CLAP'} 3 {'DOC'} default {'?'} }
    Write-Host ("  [{0,-4}] {1}" -f $catName, $e.RelPath)
}
