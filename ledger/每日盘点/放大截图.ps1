param(
    [Parameter(Mandatory = $true)][string]$Src,
    [string]$Dst = "",
    [double]$Scale = 3
)

# 把游戏内道具行囊/摊位截图放大，便于一次性看清整格的灵饰图标
# 用法： powershell -File 放大截图.ps1 -Src "C:\...\a.png" [-Scale 3] [-Dst "C:\...\out.png"]

Add-Type -AssemblyName System.Drawing

$srcPath = (Resolve-Path -LiteralPath $Src).Path
$img = [System.Drawing.Image]::FromFile($srcPath)
$w = [int]($img.Width * $Scale)
$h = [int]($img.Height * $Scale)
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$g.DrawImage($img, 0, 0, $w, $h)
$g.Dispose()

if ([string]::IsNullOrWhiteSpace($Dst)) {
    $base = [IO.Path]::GetFileNameWithoutExtension($srcPath)
    $Dst = Join-Path $env:TEMP ("zoom_" + $base + ".png")
}
$bmp.Save($Dst, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$img.Dispose()
Write-Output ("{0} -> {1} ({2}x{3})" -f $srcPath, $Dst, $w, $h)
