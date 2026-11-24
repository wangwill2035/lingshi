# 梦幻西游现金巡检 v3 - 修正坐标+每次重新激活
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class GM3 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);
    [DllImport("user32.dll")] public static extern void keybd_event(byte bVk, byte bScan, uint dwFlags, UIntPtr dwExtraInfo);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int X, int Y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint cButtons, UIntPtr dwExtraInfo);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder lpString, int nMaxCount);
}
"@

$game = [IntPtr]198576
$outDir = "$env:TEMP\mhxy_scan3"
if (Test-Path $outDir) { Remove-Item $outDir -Recurse -Force }
New-Item -ItemType Directory -Path $outDir | Out-Null

function Activate-Game {
    param([IntPtr]$hwnd)
    [GM3]::ShowWindow($hwnd, 9) | Out-Null
    $fg = [GM3]::GetForegroundWindow()
    $fgPid = 0; $fgThread = [GM3]::GetWindowThreadProcessId($fg, [ref]$fgPid)
    $myThread = [GM3]::GetCurrentThreadId()
    [GM3]::AttachThreadInput($myThread, $fgThread, $true) | Out-Null
    [GM3]::SetForegroundWindow($hwnd) | Out-Null
    [GM3]::AttachThreadInput($myThread, $fgThread, $false) | Out-Null
    Start-Sleep -Milliseconds 600
}

function TakeShot($name) {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    $bmp = New-Object System.Drawing.Bitmap(1920, 1080)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen(0, 0, 0, 0, $bmp.Size)
    $fp = Join-Path $outDir $name
    $bmp.Save($fp, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
}

# 标签坐标
$tabs = @(325, 415, 495, 575)
$results = @()

for ($i = 0; $i -lt 4; $i++) {
    # 激活游戏
    Activate-Game $game

    # 点标签
    [GM3]::SetCursorPos($tabs[$i], 40)
    Start-Sleep -Milliseconds 300
    [GM3]::mouse_event(0x02, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    [GM3]::mouse_event(0x04, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 1500

    # 再激活一次（确保游戏有焦点）
    Activate-Game $game

    # 读标题
    $sb = New-Object Text.StringBuilder 512
    [GM3]::GetWindowText($game, $sb, 512) | Out-Null
    $title = $sb.ToString()

    # Alt+E 开背包
    [GM3]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 80
    [GM3]::keybd_event(0x45, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 80
    [GM3]::keybd_event(0x45, 0, 2, [UIntPtr]::Zero)
    [GM3]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 1500

    # 截图
    TakeShot "acct_$($i+1).png"

    # 关背包
    Activate-Game $game
    [GM3]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60
    [GM3]::keybd_event(0x45, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 60
    [GM3]::keybd_event(0x45, 0, 2, [UIntPtr]::Zero)
    [GM3]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 500

    $results += "acct_$($i+1) (tab x=$($tabs[$i])): $title"
}

$results | Out-File "$outDir\results.txt" -Encoding utf8
"DONE"
