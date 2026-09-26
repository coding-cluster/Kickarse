#requires -Version 5.1
<#
.SYNOPSIS
  Draws a simple multi-resolution .ico programmatically (System.Drawing / GDI+) -- nothing is
  downloaded, no third-party asset. The mark is a rounded square with the product's ducking
  envelope drawn as a white polyline, echoing the plugin's core "envelope over one cycle" idea.

.PARAMETER OutFile
  Path to write the .ico file to.
#>
param(
    [Parameter(Mandatory = $true)][string]$OutFile
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function New-KickarseIconFrame([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    try {
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $g.Clear([System.Drawing.Color]::Transparent)

        $pad = [Math]::Max(1, [int]($size * 0.04))
        $rectSide = $size - 2*$pad
        # NOTE: PowerShell's comma (list) operator binds tighter than binary '-', so an
        # un-parenthesized "$pad, $size - 2*$pad" would parse as "($pad, $size) - (2*$pad)"
        # and blow up on array arithmetic. Pre-computing $rectSide sidesteps that entirely.
        $rect = New-Object System.Drawing.Rectangle($pad, $pad, $rectSide, $rectSide)
        $radius = [Math]::Max(2, [int]($size * 0.22))

        # rounded-rect background path
        $path = New-Object System.Drawing.Drawing2D.GraphicsPath
        $d = $radius * 2
        $path.AddArc($rect.X, $rect.Y, $d, $d, 180, 90)
        $path.AddArc($rect.Right - $d, $rect.Y, $d, $d, 270, 90)
        $path.AddArc($rect.Right - $d, $rect.Bottom - $d, $d, $d, 0, 90)
        $path.AddArc($rect.X, $rect.Bottom - $d, $d, $d, 90, 90)
        $path.CloseFigure()

        $c1 = [System.Drawing.Color]::FromArgb(255, 30, 34, 46)
        $c2 = [System.Drawing.Color]::FromArgb(255, 235, 92, 58)
        $brush = New-Object System.Drawing.Drawing2D.LinearGradientBrush($rect, $c1, $c2, 45.0)
        $g.FillPath($brush, $path)

        # envelope / duck curve, in white, roughly: up - sharp dip - recover - up
        $w = $rect.Width; $h = $rect.Height; $x0 = $rect.X; $y0 = $rect.Y
        $pts = @(
            (New-Object System.Drawing.PointF(($x0 + $w*0.12), ($y0 + $h*0.34))),
            (New-Object System.Drawing.PointF(($x0 + $w*0.34), ($y0 + $h*0.34))),
            (New-Object System.Drawing.PointF(($x0 + $w*0.46), ($y0 + $h*0.74))),
            (New-Object System.Drawing.PointF(($x0 + $w*0.58), ($y0 + $h*0.34))),
            (New-Object System.Drawing.PointF(($x0 + $w*0.88), ($y0 + $h*0.34)))
        )
        $penWidth = [Math]::Max(1.0, $size * 0.07)
        $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::White, $penWidth)
        $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
        $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
        $g.DrawLines($pen, $pts)

        $pen.Dispose(); $brush.Dispose(); $path.Dispose()
    } finally {
        $g.Dispose()
    }
    return $bmp
}

function Get-PngBytes([System.Drawing.Bitmap]$bmp) {
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    return $ms.ToArray()
}

$sizes = @(16, 32, 48, 256)
$images = @()
foreach ($s in $sizes) {
    $bmp = New-KickarseIconFrame -size $s
    # Cast back to [byte[]] explicitly: a function "return"ing a byte array gets unrolled onto
    # the pipeline and reassembled as System.Object[] (boxed bytes) by the caller, which then
    # silently binds to the wrong BinaryWriter.Write() overload below (Write(Boolean) instead of
    # Write(Byte[])) and writes almost nothing. The cast forces it back to a real Byte[].
    [byte[]]$pngBytes = Get-PngBytes $bmp
    $images += [PSCustomObject]@{ Size = $s; Png = $pngBytes }
    $bmp.Dispose()
}

$fs = [System.IO.File]::Open($OutFile, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
$bw = New-Object System.IO.BinaryWriter($fs)
try {
    # ICONDIR
    $bw.Write([UInt16]0)            # reserved
    $bw.Write([UInt16]1)            # type = icon
    $bw.Write([UInt16]$images.Count)

    $headerSize = 6 + 16 * $images.Count
    $offset = $headerSize
    foreach ($img in $images) {
        $dim = if ($img.Size -ge 256) { 0 } else { $img.Size }  # 0 means 256 per ICO spec
        $bw.Write([byte]$dim)          # width
        $bw.Write([byte]$dim)          # height
        $bw.Write([byte]0)             # color count (0 = no palette)
        $bw.Write([byte]0)             # reserved
        $bw.Write([UInt16]1)           # planes
        $bw.Write([UInt16]32)          # bit count
        $bw.Write([UInt32]$img.Png.Length)
        $bw.Write([UInt32]$offset)
        $offset += $img.Png.Length
    }
    foreach ($img in $images) {
        $bw.Write($img.Png)
    }
} finally {
    $bw.Close()
    $fs.Close()
}

Write-Host "GenerateIcon: wrote $OutFile ($($images.Count) sizes: $($sizes -join ', '))"
