# Render the same single-character labels as the Windows input indicator.
# These are UI text glyphs, not variations of the product logo.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$assets = Join-Path $PSScriptRoot '..\assets'
foreach ($mode in @(
    @{ Name = 'korean'; Glyph = [string][char]0xAC00; Font = 'Malgun Gothic' },
    @{ Name = 'english'; Glyph = 'A'; Font = 'Segoe UI' }
)) {
    $bitmap = [Drawing.Bitmap]::new(256, 256, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $font = [Drawing.Font]::new($mode.Font, 192, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
    $format = [Drawing.StringFormat]::new([Drawing.StringFormat]::GenericTypographic)
    try {
        $graphics.Clear([Drawing.Color]::Transparent)
        $graphics.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit
        $format.Alignment = [Drawing.StringAlignment]::Center
        $format.LineAlignment = [Drawing.StringAlignment]::Center
        $graphics.DrawString($mode.Glyph, $font, [Drawing.Brushes]::White,
            [Drawing.RectangleF]::new(0, 0, 256, 256), $format)
        $png = Join-Path $assets ('input-mode-' + $mode.Name + '.png')
        $bitmap.Save($png, [Drawing.Imaging.ImageFormat]::Png)
    } finally { $format.Dispose(); $font.Dispose(); $graphics.Dispose(); $bitmap.Dispose() }
    & (Join-Path $PSScriptRoot 'build-brand-icon.ps1') -Source $png -Destination (Join-Path $assets ('input-mode-' + $mode.Name + '.ico'))
}
