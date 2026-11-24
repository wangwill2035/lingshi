# ============================================================
# 管家截图工具 v1.0
# 用法：运行后全屏变暗，按住左键拖出选区，松开即截图。
#   截图自动 ① 存入 图片\截图\ 文件夹  ② 复制到剪贴板（直接 Ctrl+V 粘贴）
#   ESC 取消；选中太小的区域会自动忽略。
# 不依赖 Windows 自带截图工具，走系统底层屏幕复制，任何界面都能截。
# ============================================================
$ErrorActionPreference = 'Stop'
$errLog = Join-Path $env:TEMP 'snip-error.txt'
try {
    # DPI 感知：保证缩放显示下坐标仍是物理像素
    Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
public class SnipDpi {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@
    [SnipDpi]::SetProcessDPIAware() | Out-Null

    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing

    $outDir = Join-Path ([Environment]::GetFolderPath('MyPictures')) '截图'
    if (-not (Test-Path -LiteralPath $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }

    $screen = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $overlay = New-Object System.Windows.Forms.Form
    $overlay.FormBorderStyle = 'None'
    $overlay.StartPosition = 'Manual'
    $overlay.Bounds = $screen
    $overlay.TopMost = $true
    $overlay.ShowInTaskbar = $false
    $overlay.BackColor = 'Black'
    $overlay.Opacity = 0.35
    $overlay.Cursor = [System.Windows.Forms.Cursors]::Cross
    $overlay.KeyPreview = $true

    $script:startPt = $null
    $script:sel = $null
    $script:done = $false

    $overlay.Add_Paint({
        param($s, $e)
        if ($script:sel) {
            $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::Lime, 2)
            $e.Graphics.DrawRectangle($pen, $script:sel)
            $pen.Dispose()
        }
    })
    $overlay.Add_MouseDown({
        param($s, $e)
        if ($e.Button -eq [System.Windows.Forms.MouseButtons]::Left) {
            $script:startPt = $e.Location
        }
    })
    $overlay.Add_MouseMove({
        param($s, $e)
        if ($script:startPt) {
            $x1 = [math]::Min($script:startPt.X, $e.X); $y1 = [math]::Min($script:startPt.Y, $e.Y)
            $x2 = [math]::Max($script:startPt.X, $e.X); $y2 = [math]::Max($script:startPt.Y, $e.Y)
            $script:sel = New-Object System.Drawing.Rectangle($x1, $y1, ($x2 - $x1), ($y2 - $y1))
            $overlay.Invalidate(); $overlay.Update()
        }
    })
    $overlay.Add_MouseUp({
        param($s, $e)
        if ($e.Button -eq [System.Windows.Forms.MouseButtons]::Left -and $script:sel -and $script:sel.Width -ge 3 -and $script:sel.Height -ge 3) {
            $script:done = $true
            $overlay.Close()
        }
    })
    $overlay.Add_KeyDown({
        param($s, $e)
        if ($e.KeyCode -eq [System.Windows.Forms.Keys]::Escape) {
            $script:done = $false
            $overlay.Close()
        }
    })
    $overlay.Add_Shown({ $overlay.Activate() })

    [System.Windows.Forms.Application]::Run($overlay)
    if (-not $script:done) { exit 0 }

    $bmp = New-Object System.Drawing.Bitmap($script:sel.Width, $script:sel.Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($script:sel.X, $script:sel.Y, 0, 0, $script:sel.Size)
    $g.Dispose()

    [System.Windows.Forms.Clipboard]::SetImage($bmp)

    $fp = Join-Path $outDir ("截图-" + (Get-Date -Format 'yyyyMMdd-HHmmss') + ".png")
    $bmp.Save($fp, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()

    [System.Media.SystemSounds]::Exclamation.Play()
} catch {
    [IO.File]::WriteAllText($errLog, ($_ | Out-String), (New-Object Text.UTF8Encoding $false))
    throw
}
