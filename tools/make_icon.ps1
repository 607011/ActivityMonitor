# Generates app.ico: a small bar-chart glyph (light green bars on a dark
# green background) at multiple sizes, matching the app's on-screen theme.
Add-Type -AssemblyName System.Drawing

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$bg = [System.Drawing.Color]::FromArgb(255, 0x05, 0x14, 0x05)
$panelBg = [System.Drawing.Color]::FromArgb(255, 0x06, 0x1F, 0x06)
$bar = [System.Drawing.Color]::FromArgb(255, 0x39, 0xFF, 0x14)

function New-IconBitmap([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $margin = [Math]::Max(1, [int]($size * 0.06))
    $rectSize = $size - 2 * $margin
    $radius = [Math]::Max(1, [int]($size * 0.16))

    # Rounded-rect background panel
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $radius * 2
    $path.AddArc($margin, $margin, $d, $d, 180, 90)
    $path.AddArc($margin + $rectSize - $d, $margin, $d, $d, 270, 90)
    $path.AddArc($margin + $rectSize - $d, $margin + $rectSize - $d, $d, $d, 0, 90)
    $path.AddArc($margin, $margin + $rectSize - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $bgBrush = New-Object System.Drawing.SolidBrush $bg
    $g.FillPath($bgBrush, $path)

    # Inner panel, slightly inset
    $inset = [Math]::Max(1, [int]($size * 0.10))
    $panelRect = New-Object System.Drawing.Rectangle ($margin + $inset), ($margin + $inset), ($rectSize - 2*$inset), ($rectSize - 2*$inset)
    $panelBrush = New-Object System.Drawing.SolidBrush $panelBg
    $g.FillRectangle($panelBrush, $panelRect)

    # Four bars of increasing height, like a mini CPU history chart
    $barBrush = New-Object System.Drawing.SolidBrush $bar
    $barCount = 4
    $gap = [Math]::Max(1, [int]($panelRect.Width * 0.06))
    $barWidth = [Math]::Max(1, [int](($panelRect.Width - ($barCount + 1) * $gap) / $barCount))
    $heights = @(0.35, 0.55, 0.75, 0.95)
    for ($i = 0; $i -lt $barCount; $i++) {
        $bh = [int]($panelRect.Height * $heights[$i])
        $bx = $panelRect.X + $gap + $i * ($barWidth + $gap)
        $by = $panelRect.Y + $panelRect.Height - $bh
        $g.FillRectangle($barBrush, $bx, $by, $barWidth, $bh)
    }

    $g.Dispose()
    return $bmp
}

$bitmaps = $sizes | ForEach-Object { New-IconBitmap $_ }

# Write a multi-image .ico with PNG-compressed frames (supported since Vista).
$outPath = Join-Path $PSScriptRoot "..\app.ico"
$fs = [System.IO.File]::Open($outPath, [System.IO.FileMode]::Create)
$bw = New-Object System.IO.BinaryWriter $fs

$count = $bitmaps.Count
$bw.Write([UInt16]0)      # reserved
$bw.Write([UInt16]1)      # type: icon
$bw.Write([UInt16]$count)

$pngBlobs = @()
foreach ($bmp in $bitmaps) {
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngBlobs += ,($ms.ToArray())
}

$headerSize = 6
$dirEntrySize = 16
$offset = $headerSize + $dirEntrySize * $count

for ($i = 0; $i -lt $count; $i++) {
    $size = $sizes[$i]
    $wByte = if ($size -ge 256) { 0 } else { $size }
    $hByte = if ($size -ge 256) { 0 } else { $size }
    $bw.Write([byte]$wByte)
    $bw.Write([byte]$hByte)
    $bw.Write([byte]0)    # color palette
    $bw.Write([byte]0)    # reserved
    $bw.Write([UInt16]1)  # color planes
    $bw.Write([UInt16]32) # bits per pixel
    $bw.Write([UInt32]$pngBlobs[$i].Length)
    $bw.Write([UInt32]$offset)
    $offset += $pngBlobs[$i].Length
}

foreach ($blob in $pngBlobs) {
    $bw.Write($blob)
}

$bw.Flush()
$bw.Close()
$fs.Close()

foreach ($bmp in $bitmaps) { $bmp.Dispose() }

Write-Host "Wrote $outPath"
